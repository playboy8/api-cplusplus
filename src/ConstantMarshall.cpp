/*
 * ConstantMarshall.cpp
 *
 *  Created on: Oct 3, 2013
 *      Author: dzhou
 */

#include "ConstantMarshall.h"
#include "ScalarImp.h"
#include "TableImp.h"
#include "Util.h"

namespace dolphindb {

short ConstantMarshallImp::encodeFlag(const ConstantSP& target){
	short flag = target->getForm() <<8;
	if(target->isTable())
		flag += ((Table*)target.get())->getTableType();
	else
		flag += (target->transferAsString() && target->getType() == DT_SYMBOL) ? DT_STRING: target->getType();
	return flag;
}

IO_ERR ConstantMarshallImp::flush(){
	return out_.getDataOutputStream()->flush();
}

void ConstantUnmarshallImp::decodeFlag(short flag, DATA_FORM& form, DATA_TYPE& type){
	form = static_cast<DATA_FORM>(flag >> 8);
	type = static_cast<DATA_TYPE>(flag & 0xff);
}

bool ScalarMarshall::start(const char* requestHeader, size_t headerSize, const ConstantSP& target, bool blocking, IO_ERR& ret){
	if(headerSize > 1024){
		ret = INVALIDDATA;
		return false;
	}
	else if(headerSize > 0)
		memcpy(buf_, requestHeader, headerSize);

	short flag = encodeFlag(target);
	memcpy(buf_ + headerSize, (char*)&flag, 2);
	headerSize += sizeof(short);

	target_.clear();
	partial_ = 0;
	complete_ = false;

	int numElement =0;
	int actualSize = 0;
	actualSize = target->serialize(buf_ + headerSize, MARSHALL_BUFFER_SIZE - headerSize,0,0,numElement,partial_);
	if(actualSize < 0){
		ret = OTHERERR;
		return false;
	}
	if(!blocking)
		target_ = target;

	ret =out_.start(buf_,actualSize + headerSize);

	while(ret == OK && numElement == 0){
		actualSize = target->serialize(buf_,MARSHALL_BUFFER_SIZE,0,partial_,numElement,partial_);
		if(actualSize < 0){
			ret = OTHERERR;
			return false;
		}
		ret =out_.start(buf_,actualSize);
	}

	if(numElement == 1){
		target_.clear();
		partial_ = 0;
	}

	complete_ = (ret == OK);
	return complete_;
}

void ScalarMarshall::reset(){
	target_.clear();
}

bool VectorMarshall::start(const ConstantSP& target, bool blocking, IO_ERR& ret){
	return start(0, 0, target, blocking, ret);
}

bool VectorMarshall::start(const char* requestHeader, size_t headerSize, const ConstantSP& target, bool blocking, IO_ERR& ret){
	if(headerSize > 1024){
		ret = INVALIDDATA;
		return false;
	}
	else if(headerSize > 0)
		memcpy(buf_, requestHeader, headerSize);
	size_t offset = headerSize;

	target_.clear();
	marshall_.clear();
	partial_ = 0;
	nextStart_ = 0;
	complete_ = false;

	short flag = encodeFlag(target);
	memcpy(buf_ + offset, (char*)&flag, sizeof(short));
	offset += sizeof(short);

	int rows = target->rows();
	int cols = target->columns();
	memcpy(buf_ + offset, (char*)&rows, sizeof(int));
	offset += sizeof(int);
	memcpy(buf_ + offset, (char*)&cols, sizeof(int));
	offset += sizeof(int);

	int numElement =0;
	INDEX size = target->size();
	VectorSP vec = target;
	if(vec->isView())
		vec = target->getValue();

	INDEX actualSize = 0;
	if(size>0 && vec->getType() != DT_ANY){
		actualSize = vec->serialize(buf_+offset,MARSHALL_BUFFER_SIZE-offset,0,0,numElement,partial_);
		if(actualSize < 0){
			ret = OTHERERR;
			return false;
		}
		nextStart_ += numElement;
	}

	if(!blocking)
		target_ = vec;
	ret =out_.start(buf_,actualSize + offset);

	if(vec->getType() == DT_ANY){
		while(ret == OK && nextStart_ < size){
			ConstantSP element = vec->get(nextStart_);
			marshall_ = ConstantMarshallFactory::getInstance(element->getForm(), out_.getDataOutputStream());
			if(marshall_->start(element, blocking, ret))
				++nextStart_;
		}
	}
	else{
		while(ret == OK && nextStart_ < size){
			actualSize = vec->serialize(buf_,MARSHALL_BUFFER_SIZE,nextStart_,partial_,numElement,partial_);
			if(actualSize < 0){
				ret = OTHERERR;
				return false;
			}
			else
				nextStart_ += numElement;
			ret =out_.start(buf_,actualSize);
		}
	}

	complete_ = (ret == OK);
	return complete_;
}

void VectorMarshall::reset(){
	target_.clear();
	if(!marshall_.isNull())
		marshall_.clear();
}

bool MatrixMarshall::sendMeta(const char* requestHeader, size_t headerSize, const ConstantSP& target, bool blocking, IO_ERR& ret){
	if(headerSize > 1024){
		ret = INVALIDDATA;
		return false;
	}
	else if(headerSize > 0)
		memcpy(buf_, requestHeader, headerSize);

	short flag = encodeFlag(target);
	memcpy(buf_ + headerSize, (char*)&flag, sizeof(short));
	headerSize += sizeof(short);
	char hasLabels = (target->getRowLabel()->isNull()? 0 : 1) + (target->getColumnLabel()->isNull()? 0 : 2);
	memcpy(buf_ + headerSize, &hasLabels, sizeof(char));
	headerSize += sizeof(char);
	ret =out_.start(buf_, headerSize);
	return ret==OK;
}

bool MatrixMarshall::start(const char* requestHeader, size_t headerSize, const ConstantSP& target, bool blocking, IO_ERR& ret){
	target_.clear();
	rowLabelSent_ = false;
	columnLabelSent_ = false;
	inProgress_ = false;
	complete_ = false;

	if(!blocking)
		target_ = target;
	if(!sendMeta(requestHeader, headerSize, target, blocking, ret))
		return false;

	inProgress_ = true;

	if(!target->getRowLabel()->isNull()){
		if(!vectorMarshall_.start(target->getRowLabel(), blocking, ret))
			return false;
	}
	rowLabelSent_ = true;

	if(!target->getColumnLabel()->isNull()){
		if(!vectorMarshall_.start(target->getColumnLabel(), blocking, ret))
			return false;
	}
	columnLabelSent_ = true;

	vectorMarshall_.start(target, blocking, ret);
	complete_ = (ret == OK);
	return complete_;
}

void MatrixMarshall::reset(){
	if(!target_.isNull())
		target_.clear();
	vectorMarshall_.reset();
}

bool TableMarshall::sendMeta(const char* requestHeader, size_t headerSize, const ConstantSP& target, bool blocking, IO_ERR& ret){
	if(headerSize > 1024){
		ret = INVALIDDATA;
		return false;
	}
	else if(headerSize > 0)
		memcpy(buf_, requestHeader, headerSize);

	short flag = encodeFlag(target);
	memcpy(buf_ + headerSize, (char*)&flag,sizeof(short));
	headerSize += sizeof(short);

	int rows = target->rows();
	int cols = target->columns();
	memcpy(buf_ + headerSize, (char*)&rows, sizeof(int));
	headerSize += sizeof(int);

	memcpy(buf_ + headerSize, (char*)&cols,sizeof(int));
	headerSize += sizeof(int);

	Table* table= static_cast<Table*>(target.get());
	//serialize table name
	strcpy(buf_+ headerSize, table->getName().c_str());
	headerSize += table->getName().size() + 1;


	//serialize column names
	while(columnNamesSent_ < cols){
		const string& name = table->getColumnName(columnNamesSent_);
		int strLen = name.size() + 1;
		if(headerSize + strLen <= MARSHALL_BUFFER_SIZE){
			strcpy(buf_ + headerSize, name.c_str());
			headerSize += strLen;
			++columnNamesSent_;
		}
		else{
			ret = out_.start(buf_, headerSize);
			if(ret != OK)
				return false;
			headerSize = 0;
		}
	}

	if(headerSize > 0){
		ret = out_.start(buf_, headerSize);
		return ret == OK;
	}
	else
		return true;
}

bool TableMarshall::start(const char* requestHeader, size_t headerSize, const ConstantSP& target, bool blocking, IO_ERR& ret){
	target_.clear();
	columnNamesSent_ = 0;
	nextColumn_ = 0;
	columnInProgress_ = false;
	complete_ = false;

	TableSP table(target);
	if(!blocking)
		target_ = table;
	if(!sendMeta(requestHeader, headerSize, table, blocking, ret))
		return false;

	ret = OK;
	while(nextColumn_ < table->columns() && ret==OK){
		vectorMarshall_.start(table->getColumn(nextColumn_), blocking, ret);
		if(ret == OK)
			nextColumn_++;
		else
			columnInProgress_ = true;
	}
	complete_ = (ret == OK);
	return complete_;
}

void TableMarshall::reset(){
	target_.clear();
	vectorMarshall_.reset();
}

bool SetMarshall::sendMeta(const char* requestHeader, size_t headerSize, const ConstantSP& target, bool blocking, IO_ERR& ret){
	if(headerSize > 1024){
		ret = INVALIDDATA;
		return false;
	}
	else if(headerSize > 0)
		memcpy(buf_, requestHeader, headerSize);

	short flag = encodeFlag(target);
	memcpy(buf_ + headerSize, (char*)&flag, sizeof(short));
	headerSize += sizeof(short);
	return (ret = out_.start(buf_, headerSize)) == OK;
}

bool SetMarshall::start(const char* requestHeader, size_t headerSize, const ConstantSP& target, bool blocking, IO_ERR& ret){
	target_.clear();
	complete_ = false;

	if(!blocking)
		target_ = target;
	if(!sendMeta(requestHeader, headerSize, target, blocking, ret))
		return false;

	ConstantSP keys = ((Set*)target.get())->keys();
	vectorMarshall_.start(keys, blocking, ret);
	complete_ = (ret == OK);
	return complete_;
}

void SetMarshall::reset(){
	target_.clear();
	vectorMarshall_.reset();
}

bool DictionaryMarshall::sendMeta(const char* requestHeader, size_t headerSize, const ConstantSP& target, bool blocking, IO_ERR& ret){
	if(headerSize > 1024){
		ret = INVALIDDATA;
		return false;
	}
	else if(headerSize > 0)
		memcpy(buf_, requestHeader, headerSize);

	short flag = encodeFlag(target);
	memcpy(buf_ + headerSize, (char*)&flag, sizeof(short));
	headerSize += sizeof(short);
	return (ret = out_.start(buf_, headerSize)) == OK;
}

bool DictionaryMarshall::start(const char* requestHeader, size_t headerSize, const ConstantSP& target, bool blocking, IO_ERR& ret){
	target_.clear();
	keySent_ = false;
	inProgress_ = false;
	complete_ = false;

	if(!blocking)
		target_ = target;
	if(!sendMeta(requestHeader, headerSize, target, blocking, ret))
		return false;

	inProgress_ = true;
	ConstantSP keys = ((Dictionary*)target.get())->keys();
	if(!vectorMarshall_.start(keys, blocking, ret))
		return false;

	keySent_ = true;
	ConstantSP values = ((Dictionary*)target.get())->values();
	vectorMarshall_.start(values, blocking, ret);
	complete_ = (ret == OK);
	return complete_;
}

void DictionaryMarshall::reset(){
	target_ = ConstantSP();
	vectorMarshall_.reset();
}

bool ChunkMarshall::start(const char* requestHeader, size_t headerSize, const ConstantSP& target, bool blocking, IO_ERR& ret){
	if(headerSize > 1024){
		ret = INVALIDDATA;
		return false;
	}

	complete_ = false;
	DFSChunkMeta* chunk = (DFSChunkMeta*)target.get();
	Buffer buffer(buf_, headerSize + 256);
	if(headerSize > 0)
		buffer.write(requestHeader, headerSize);
	short flag = encodeFlag(target);
	buffer.write(flag);
	buffer.write((short)0); //size of the following data. will change this part later
	buffer.write(chunk->getPath());
	buffer.write((const char*)chunk->getId().bytes(),16);
	buffer.write(chunk->getVersion());
	buffer.write(chunk->size());
	buffer.write((char)chunk->getChunkType());
	int copyCount = chunk->getCopyCount();
	buffer.write((char)chunk->getCopyCount());
	for(int i=0; i<copyCount; ++i){
		ret = buffer.write(chunk->getCopySite(i));
		if(ret != OK)
			return false;
	}
	ret = buffer.write(chunk->getCommitId());
	if(ret != OK)
		return false;

	//set bytearray size
	int count = buffer.size();
	short byteArraySize = count - 4;
	memcpy(buf_ + 2, &byteArraySize, 2);

	complete_ = ((ret = out_.start(buf_, count)) == OK);
	return complete_;
}

void  ChunkMarshall::reset(){}

bool ScalarUnmarshall::start(short flag, bool blocking, IO_ERR& ret){
	DATA_FORM form;
	DATA_TYPE type;
	decodeFlag(flag, form, type);
	if(type == DT_FUNCTIONDEF){
		isCodeObject_ = true;
		functionType_ = -1;
		if((ret = in_->readChar(functionType_)) != OK)
			return false;
		else if(functionType_ < 0){
			ret = INVALIDDATA;
			return false;
		}
		obj_ = Util::createConstant(DT_STRING);
		INDEX numElement = 0;
		ret = obj_->deserialize(in_.get(), 0, 1, numElement);
		return ret == OK;
	}
	else{
		isCodeObject_ = false;
		obj_ = Util::createConstant(type);
		if(obj_.isNull()){
			ret = INVALIDDATA;
			return false;
		}
		INDEX numElement = 0;
		ret = obj_->deserialize(in_.get(), 0, 1, numElement);
		return ret == OK;
	}
}

void ScalarUnmarshall::reset(){
	obj_.clear();
}

bool SymbolBaseUnmarshall::start(bool blocking, IO_ERR& ret){
	symbaseId_ = -1;
	size_ = -1;

	if((ret = in_->readInt(symbaseId_)) != OK)
		return false;
	else if(symbaseId_ < 0){
		ret = INVALIDDATA;
		return false;
	}

	if((ret = in_->readInt(size_)) != OK)
		return false;
	else if(size_ < 0){
		ret = INVALIDDATA;
		return false;
	}

	if(size_ == 0){
		obj_ = dict_[symbaseId_];
		if(obj_.isNull())
			ret = INVALIDDATA;
		return ret == OK;
	}

	SymbolBaseSP cache = new vector<string>();
	vector<string>& symbols = *cache;
	symbols.reserve(size_);
	string sym;
	while(symbols.size() < (size_t)size_){
		ret = in_->readString(sym);
		if(UNLIKELY(ret != OK))
			return false;
		else
			symbols.push_back(sym);
	}
	obj_ = cache;
	dict_[symbaseId_] = obj_;
	return ret == OK;
}

void SymbolBaseUnmarshall::reset(){
	obj_.clear();
	dict_.clear();
}

void VectorUnmarshall::resetSymbolBaseUnmarshall(bool createIfNotExist){
	if(!symbaseUnmarshall_.isNull()){
		symbaseUnmarshall_->reset();
	}
	else if(createIfNotExist){
		symbaseUnmarshall_ = new SymbolBaseUnmarshall(in_);
	}
}

bool VectorUnmarshall::start(short flag, bool blocking, IO_ERR& ret){
	flag_ = flag;
	rows_ = -1;
	columns_ = -1;
	if((ret = in_->readInt(rows_)) != OK)
		return false;
	else if(rows_ < 0){
		ret = INVALIDDATA;
		return false;
	}
	if((ret = in_->readInt(columns_)) != OK)
		return false;
	else if(columns_ < 0){
		ret = INVALIDDATA;
		return false;
	}
	DATA_FORM form;
	DATA_TYPE type;
	decodeFlag(flag, form, type);
	if(form == DF_PAIR)
		obj_ = Util::createPair(type);
	else if(form == DF_VECTOR)
		obj_ = Util::createVector(type, rows_);
	else if(form == DF_MATRIX)
		obj_ = Util::createMatrix(type, columns_, rows_, columns_);
	else{
		ret = INVALIDDATA;
		return false;
	}
	if(obj_.isNull()){
		ret = INVALIDDATA;
		return false;
	}
	nextStart_ = 0;
	if(rows_ == 0)
		return true;

	if(type == DT_ANY){
		ret = OK;
		if(!unmarshall_.isNull())
			unmarshall_.clear();
		while(ret == OK && nextStart_ < rows_){
			ret = in_->readShort(flag);
			if(ret == OK){
				decodeFlag(flag, form, type);
				unmarshall_ = ConstantUnmarshallFactory::getInstance(form, in_);
				if(unmarshall_.isNull()){
					ret = INVALIDDATA;
				}
				else{
					unmarshall_->start(flag, blocking, ret);
					if(ret == OK){
						obj_->set(nextStart_, unmarshall_->getConstant());
						++nextStart_;
						unmarshall_.clear();
					}
				}
			}
		}
	}
	else if(type != DT_SYMBOL + 128){
		INDEX numElements = 0;
		ret = obj_->deserialize(in_.get(), 0, obj_->size(), numElements);
		nextStart_ = numElements;
	}
	else{
		if(symbaseUnmarshall_.isNull())
			resetSymbolBaseUnmarshall(true);
		if(!symbaseUnmarshall_->start(blocking, ret))
			return false;

		string* pdata = (string*)obj_->getDataArray();
		vector<string>& symbols = *symbaseUnmarshall_->getSymbolBase();
		size_t unitLength = sizeof(int);
		if(!in_->isIntegerReversed()){
			size_t actualLength;
			int buf[Util::BUF_SIZE];
			while(ret == OK && nextStart_ < rows_){
				int count = std::min(rows_ - nextStart_, Util::BUF_SIZE);
				ret = in_->readBytes((char*)buf, unitLength,  count, actualLength);
				for(size_t i=0; i<actualLength; ++i)
					pdata[nextStart_ + i] = symbols[buf[i]];
				nextStart_ += actualLength;
			}
		}
		else{
			int symIndex;
			while(ret == OK && nextStart_ < rows_){
				if((ret = in_->readInt(symIndex)) != OK) continue;
				pdata[nextStart_] = symbols[symIndex];
				++nextStart_;
			}
		}
		if(ret == OK)
			obj_->setNullFlag(obj_->hasNull());
	}
	return ret == OK;
}

void VectorUnmarshall::reset(){
	obj_.clear();
	if(!unmarshall_.isNull())
		unmarshall_.clear();
}

bool MatrixUnmarshall::start(short flag, bool blocking, IO_ERR& ret){
	labelFlag_ = -1;
	rowLabelReceived_ = false;
	columnLabelReceived_ = false;
	inProgress_ = false;
	rowLabel_ = Constant::void_;
	columnLabel_ = Constant::void_;

	if((ret = in_->readChar(labelFlag_)) != OK)
		return false;
	else if(labelFlag_ < 0){
		ret = INVALIDDATA;
		return false;
	}

	if(labelFlag_ & 1){
		ret = in_->readShort(flag);
		if(ret != OK)
			return false;
		inProgress_ = true;
		if(!vectorUnmarshall_.start(flag, blocking, ret))
			return false;
		rowLabel_ = vectorUnmarshall_.getConstant();
		inProgress_ = false;
	}
	rowLabelReceived_ = true;

	if(labelFlag_ & 2){
		ret = in_->readShort(flag);
		if(ret != OK)
			return false;
		inProgress_ = true;
		if(!vectorUnmarshall_.start(flag, blocking, ret))
			return false;
		columnLabel_ = vectorUnmarshall_.getConstant();
		inProgress_ = false;
	}
	columnLabelReceived_ = true;

	ret = in_->readShort(flag);
	if(ret != OK)
		return false;
	inProgress_ = true;
	if(!vectorUnmarshall_.start(flag, blocking, ret))
		return false;

	obj_ = vectorUnmarshall_.getConstant();
	obj_->setRowLabel(rowLabel_);
	obj_->setColumnLabel(columnLabel_);
	rowLabel_.clear();
	columnLabel_.clear();
	return true;
}

void MatrixUnmarshall::reset(){
	if(!obj_.isNull())
		obj_.clear();
	if(!rowLabel_.isNull())
		rowLabel_.clear();
	if(!columnLabel_.isNull())
		columnLabel_.clear();
	vectorUnmarshall_.reset();
}

bool TableUnmarshall::start(short flag, bool blocking, IO_ERR& ret){
	type_ = (TABLE_TYPE)(flag & 255);
	rows_ = -1;
	columns_ = -1;
	tableNameReceived_ = false;

	if((ret = in_->readInt(rows_)) != OK)
		return false;
	else if(rows_ < 0){
		ret = INVALIDDATA;
		return false;
	}
	if((ret = in_->readInt(columns_)) != OK)
		return false;
	else if(columns_ <= 0){
		ret = INVALIDDATA;
		return false;
	}
	if((ret = in_->readString(tableName_)) != OK)
		return false;
	tableNameReceived_ = true;
	columnNameReceived_ = false;
	nextColumn_ = 0;
	colNames_.clear();

	string name;
	while(nextColumn_ < columns_){
		ret = in_->readString(name);
		if(ret != OK)
			return false;
		colNames_.push_back(name);
		++nextColumn_;
	}
	columnNameReceived_ = true;
	nextColumn_ = 0;
	inProgress_ = false;
	colObjs_.clear();
	while(nextColumn_ < columns_){
		ret = in_->readShort(flag);
		if(ret != OK)
			return false;
		inProgress_ = true;
		if(vectorUnmarshall_.start(flag, blocking, ret)){
			inProgress_ = false;
			++nextColumn_;
			colObjs_.push_back(vectorUnmarshall_.getConstant());
		}
		else
			return false;
	}
	obj_ = new BasicTable(colObjs_, colNames_, {});
	colNames_.clear();
	colObjs_.clear();
	return true;
}
void TableUnmarshall::reset(){
	obj_.clear();
	colNames_.clear();
	colObjs_.clear();
	vectorUnmarshall_.reset();
	vectorUnmarshall_.resetSymbolBaseUnmarshall(false);
}

bool SetUnmarshall::start(short flag, bool blocking, IO_ERR& ret){
	inProgress_ = false;

	//read key vector
	ret = in_->readShort(flag);
	if(ret != OK)
		return false;

	inProgress_ = true;
	if(vectorUnmarshall_.start(flag, blocking, ret)){
		ConstantSP keyVector = vectorUnmarshall_.getConstant();
		SetSP set = Util::createSet(keyVector->getType(),0);
		set->append(keyVector);
		obj_ = ConstantSP(set);
		return true;
	}
	else
		return false;
}

void SetUnmarshall::reset(){
	obj_.clear();
	vectorUnmarshall_.reset();
}

bool DictionaryUnmarshall::start(short flag, bool blocking, IO_ERR& ret){
	keyReceived_ = false;
	keyVector_.clear();
	inProgress_ = false;

	//read key vector
	ret = in_->readShort(flag);
	if(ret != OK)
		return false;

	inProgress_ = true;
	if(vectorUnmarshall_.start(flag, blocking, ret)){
		keyVector_ = vectorUnmarshall_.getConstant();
		keyReceived_ = true;
		inProgress_ = false;
	}
	else
		return false;

	//read value vector
	ret = in_->readShort(flag);
	if(ret != OK)
		return false;

	inProgress_ = true;
	if(vectorUnmarshall_.start(flag, blocking, ret)){
		inProgress_ = false;
		ConstantSP valueVector = vectorUnmarshall_.getConstant();
		DictionarySP dict = Util::createDictionary(keyVector_->getType(), valueVector->getType());
		dict->set(keyVector_, valueVector);
		obj_ = ConstantSP(dict);
		keyVector_.clear();
		return true;
	}
	else
		return false;
}

void DictionaryUnmarshall::reset(){
	obj_.clear();
	keyVector_.clear();
	vectorUnmarshall_.reset();
}

bool ChunkUnmarshall::start(short flag, bool blocking, IO_ERR& ret){
	size_ = -1;
	ret = in_->readShort(size_);
	if(ret != OK)
		return false;
	if(size_ <= 0 || size_ > MARSHALL_BUFFER_SIZE){
		ret = INVALIDDATA;
		return false;
	}

	vector<char> buf;
	ret = in_->read(buf.data(), size_);
	if(ret != OK)
		return ret;

	return parsing(buf.data());
}

IO_ERR  ChunkUnmarshall::parsing(const char* buf){
	DataInputStreamSP in(new DataInputStream(buf, size_, false));
	string path;
	char guid[16];
	int version;
	int size;
	char chunkType;
	long long cid;
	vector<string> sites;

	IO_ERR ret = in->readString(path);
	if(ret != OK)
		return ret;

	in->read(guid, 16);
	in->readInt(version);
	in->readInt(size);
	in->readChar(chunkType);
	char copyCount;
	ret = in->readChar(copyCount);
	if(ret != OK)
		return ret;
	sites.reserve(copyCount);
	for(char i=0; i<copyCount; ++i){
		string site;
		if((ret = in->readString(site)) != OK)
			return ret;
		sites.push_back(site);
	}
	ret = in->readLong(cid);
	if(ret != OK)
		return ret;

	Guid id((unsigned char*)guid);
	obj_ = new DFSChunkMeta(path, id, version, size, (CHUNK_TYPE)chunkType, sites, cid);
	return OK;
}

void ChunkUnmarshall::reset(){
	size_ = 0;
	obj_.clear();
}

ConstantMarshallFactory::ConstantMarshallFactory(const DataOutputStreamSP& out){
	arrMarshall[DF_SCALAR]= new ScalarMarshall(out);
	arrMarshall[DF_VECTOR]= new VectorMarshall(out);
	arrMarshall[DF_PAIR]= new VectorMarshall(out);
	arrMarshall[DF_MATRIX]= new MatrixMarshall(out);
	arrMarshall[DF_SET]= new SetMarshall(out);
	arrMarshall[DF_DICTIONARY]= new DictionaryMarshall(out);
	arrMarshall[DF_TABLE]= new TableMarshall(out);
	arrMarshall[DF_CHART]= new DictionaryMarshall(out);
	arrMarshall[DF_CHUNK]= new ChunkMarshall(out);
}

ConstantMarshallSP ConstantMarshallFactory::getInstance(DATA_FORM form, const DataOutputStreamSP& out){
	switch(form){
		case DF_SCALAR: return new ScalarMarshall(out);
		case DF_VECTOR:
		case DF_PAIR: return new VectorMarshall(out);
		case DF_MATRIX: return new MatrixMarshall(out);
		case DF_SET: return new SetMarshall(out);
		case DF_DICTIONARY: return new DictionaryMarshall(out);
		case DF_TABLE: return new TableMarshall(out);
		case DF_CHART: return new DictionaryMarshall(out);
		case DF_CHUNK: return new ChunkMarshall(out);
		default : return ConstantMarshallSP();
	}
}

ConstantMarshallFactory::~ConstantMarshallFactory(){
	delete arrMarshall[DF_SCALAR];
	delete arrMarshall[DF_VECTOR];
	delete arrMarshall[DF_PAIR];
	delete arrMarshall[DF_MATRIX];
	delete arrMarshall[DF_SET];
	delete arrMarshall[DF_DICTIONARY];
	delete arrMarshall[DF_TABLE];
	delete arrMarshall[DF_CHART];
	delete arrMarshall[DF_CHUNK];
}

ConstantUnmarshallFactory::ConstantUnmarshallFactory(const DataInputStreamSP& in){
	arrUnmarshall[DF_SCALAR] = new ScalarUnmarshall(in);
	arrUnmarshall[DF_VECTOR] = new VectorUnmarshall(in);
	arrUnmarshall[DF_PAIR] = new VectorUnmarshall(in);
	arrUnmarshall[DF_MATRIX] = new MatrixUnmarshall(in);
	arrUnmarshall[DF_SET] = new SetUnmarshall(in);
	arrUnmarshall[DF_DICTIONARY] = new DictionaryUnmarshall(in);
	arrUnmarshall[DF_TABLE] = new TableUnmarshall(in);
	arrUnmarshall[DF_CHART] = new DictionaryUnmarshall(in);
	arrUnmarshall[DF_CHUNK] = new ChunkUnmarshall(in);
}

ConstantUnmarshallSP ConstantUnmarshallFactory::getInstance(DATA_FORM form, const DataInputStreamSP& in){
	switch(form){
		case DF_SCALAR: return new ScalarUnmarshall(in);
		case DF_VECTOR:
		case DF_PAIR: return new VectorUnmarshall(in);
		case DF_MATRIX: return new MatrixUnmarshall(in);
		case DF_SET: return new SetUnmarshall(in);
		case DF_DICTIONARY: return new DictionaryUnmarshall(in);
		case DF_TABLE: return new TableUnmarshall(in);
		case DF_CHART: return new DictionaryUnmarshall(in);
		case DF_CHUNK: return new ChunkUnmarshall(in);
		default : return ConstantUnmarshallSP();
	}
}

ConstantUnmarshallFactory::~ConstantUnmarshallFactory(){
	delete arrUnmarshall[DF_SCALAR];
	delete arrUnmarshall[DF_VECTOR];
	delete arrUnmarshall[DF_PAIR];
	delete arrUnmarshall[DF_MATRIX];
	delete arrUnmarshall[DF_SET];
	delete arrUnmarshall[DF_DICTIONARY];
	delete arrUnmarshall[DF_TABLE];
	delete arrUnmarshall[DF_CHART];
	delete arrUnmarshall[DF_CHUNK];
}

};
