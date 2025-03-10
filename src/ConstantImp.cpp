/*
 * ConstantImp.cpp
 *
 *  Created on: Sep 3, 2012
 *      Author: dzhou
 */
#include "ConstantImp.h"

namespace dolphindb {

StringVector::StringVector(INDEX size, INDEX capacity, bool blob) : blob_(blob){
	data_.reserve((std::max)(size, capacity));
	if(size > 0)
		data_.resize(size);
	containNull_ = false;
}

StringVector::StringVector(const vector<string>& data, INDEX capacity, bool containNull, bool blob) : blob_(blob) {
	data_.reserve((std::max)(data.size(), (size_t)capacity));
	data_.assign(data.begin(), data.end());
	containNull_ = containNull;
}

INDEX StringVector::reserve(INDEX capacity){
	data_.reserve(capacity);
	return data_.capacity();
}

IO_ERR StringVector::deserialize(DataInputStream* in, INDEX indexStart, INDEX targetNumElement, INDEX& numElement){
    auto readBlob = [&](string& value) -> IO_ERR {
        IO_ERR ret;
        int len;
        if ((ret = in->readInt(len)) != OK)
            return ret;
        std::unique_ptr<char[]> buf(new char[len]);
        if ((ret = in->read(buf.get(), len)) != OK)
            return ret;
        value.clear();
        value.append(buf.get(), len);
        return ret;
    };
    if(numElement >= 0){
        //read string
		numElement = 0;
		INDEX firstTarget = ((std::min))(size() - indexStart, targetNumElement);
		while(numElement < firstTarget){
            IO_ERR ret;
            if (blob_) {
                ret = readBlob(data_[indexStart]);
            } else {
                ret = in->readString(data_[indexStart]);
            }
			if(ret != OK)
				return ret;
			++indexStart;
			++numElement;
		}
		string value;
		while(numElement < targetNumElement){
            IO_ERR ret;
            if (blob_) {
                ret = readBlob(value);
            } else {
                ret = in->readString(value);
            }
            if(ret != OK)
                return ret;
            data_.push_back(value);
			++numElement;
		}
		return OK;
	}
	else{
		//read line
		numElement = 0;
		INDEX firstTarget = ((std::min))(size() - indexStart, targetNumElement);
		while(numElement < firstTarget){
			IO_ERR ret = in->readLine(data_[indexStart]);
			if(ret != OK)
				return ret;
			++indexStart;
			++numElement;
		}
		string value;
		while(numElement < targetNumElement){
			IO_ERR ret = in->readLine(value);
			if(ret != OK)
				return ret;
			data_.push_back(value);
			++numElement;
		}
		return OK;
	}
}

void StringVector::upper(){
	vector<string>::iterator it = data_.begin();
	vector<string>::iterator end = data_.end();
	while(it != end){
		size_t size = it->size();
		for(size_t i=0; i<size; ++i){
			char& ch = (*it)[i];
			if(ch>='a' && ch<='z')
				ch -= 32;
		}
		++it;
	}
}

void StringVector::lower(){
	vector<string>::iterator it = data_.begin();
	vector<string>::iterator end = data_.end();
	while(it != end){
		size_t size = it->size();
		for(size_t i=0; i<size; ++i){
			char& ch = (*it)[i];
			if(ch>='A' && ch<='Z')
				ch += 32;
		}
		++it;
	}
}

void StringVector::trim(){
	vector<string>::iterator it = data_.begin();
	vector<string>::iterator end = data_.end();
	while(it != end){
		string& s =it->erase(it->find_last_not_of(' ') + 1);
		s.erase(0, s.find_first_not_of(' '));
		if(!containNull_)
			containNull_ = s.empty();
		++it;
	}
}

void StringVector::strip(){
	vector<string>::iterator it = data_.begin();
	vector<string>::iterator end = data_.end();
	while(it != end){
		string& s =it->erase(it->find_last_not_of("\t\r\n ") + 1);
		s.erase(0, s.find_first_not_of("\t\r\n "));
		if(!containNull_)
			containNull_ = s.empty();
		++it;
	}
}

bool StringVector::set(const ConstantSP& index, const ConstantSP& value){
	if(index->isVector()){
		bool literal=value->getCategory()==LITERAL;
		INDEX len=index->size();
		INDEX bufIndex[Util::BUF_SIZE];
		char* bufVal[Util::BUF_SIZE];
		const INDEX* pindex;
		char** pval;
		INDEX start=0;
		int count;
		while(start<len){
			count=((std::min))(len-start,Util::BUF_SIZE);
			pindex=index->getIndexConst(start,count,bufIndex);
			if(literal){
				pval=value->getStringConst(start,count,bufVal);
				for(int i=0;i<count;i++)
					data_[pindex[i]] = pval[i];
			}
			else{
				for(int i=0;i<count;i++)
					data_[pindex[i]] = value->getString(start + i);
			}
			start+=count;
		}
	}
	else if(value->isScalar())
		data_[index->getIndex()] = value->isNull() ? Constant::EMPTY : value->getString();
	else
		throw RuntimeException("Size incompatible between index and value");
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

bool StringVector::assign(const ConstantSP& value){
	if(value->isVector()){
		if(size()!=value->size())
			return false;
	}
	fill(0,size(),value);
	containNull_ = value->getNullFlag();
	return true;
}

INDEX StringVector::search(const string& val){
	vector<string>::iterator it=std::find(data_.begin(),data_.end(),val);
	if(it!=data_.end())
		return it-data_.begin();
	else
		return -1;
}

ConstantSP StringVector::getValue() const {
	Vector* copy = new StringVector(data_, data_.size(), containNull_, blob_);
	copy->setForm(getForm());
	return copy;
}

ConstantSP StringVector::getSubVector(INDEX start, INDEX length, INDEX capacity) const {
	StringVector* vec=new StringVector(0, capacity, blob_);
	ConstantSP result(vec);
	if(start<0 || start>=size() || std::abs(length)>size())
		return result;

	if(length>0)
		vec->data_.insert(vec->data_.begin(),data_.begin()+start,data_.begin()+(start+length));
	else
		vec->data_.insert(vec->data_.begin(),data_.rbegin()+(size()-1-start),data_.rbegin()+(size()-1-start-length));
	result->setNullFlag(containNull_);
	return result;
}

ConstantSP StringVector::get(const ConstantSP& index) const {
	UINDEX size=data_.size();
	if(index->isVector()){
		INDEX len=index->size();
		StringVector* p=new StringVector(len, len, blob_);
		ConstantSP result(p);
		if(index->isIndexArray()){
			UINDEX* bufIndex=(UINDEX*)index->getIndexArray();
			for(INDEX i=0;i<len;++i)
				p->data_[i]=bufIndex[i]<size?data_[bufIndex[i]]:"";
		}
		else{
			const int bufSize=Util::BUF_SIZE;
			UINDEX bufIndex[bufSize];

			INDEX start=0;
			int count=0;
			int i;
			while(start<len){
				count=((std::min))(len-start,bufSize);
				index->getIndex(start,count,(INDEX*)bufIndex);
				for(i=0;i<count;i++){
					p->data_[start+i]=bufIndex[i]<size?data_[bufIndex[i]]:"";
				}
				start+=count;
			}
		}
		p->setNullFlag(containNull_ || p->hasNull());
		return result;
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new String(idx<size?data_[idx]:"", blob_));
	}
}

bool StringVector::append(const ConstantSP& value, INDEX len){
	size_t newSize;
	if((newSize = data_.size() + len) > data_.capacity())
		data_.reserve(newSize);

	if(value->getCategory()==LITERAL){
		if(value->isScalar())
			data_.push_back(value->getString());
		else{

			char* bufVal[Util::BUF_SIZE];
			char** pval;
			INDEX start=0;
			int count;
			while(start<len){
				count=((std::min))(len-start,Util::BUF_SIZE);
				pval=value->getStringConst(start,count,bufVal);
				data_.insert(data_.end(), pval, pval + count);
				start+=count;
			}
		}
	}
	else{
		for(INDEX i=0;i<len;i++)
			data_.push_back(value->getString(i));
	}
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

bool StringVector::appendString(string* buf, int len){
	size_t newSize;
	if((newSize = data_.size() + len) > data_.capacity())
		data_.reserve(newSize);

	for(int i=0;i<len;i++)
		data_.push_back(buf[i]);
	return true;
}

bool StringVector::appendString(char** buf, int len){
	size_t newSize;
	if((newSize = data_.size() + len) > data_.capacity())
		data_.reserve(newSize);

	for(int i=0;i<len;i++)
		data_.push_back(buf[i]);
	return true;
}

bool StringVector::remove(INDEX count){
	bool fromHead=(count<0);
	count=((std::min))(size(),abs(count));
	if(fromHead)
		data_.erase(data_.begin(),data_.begin()+count);
	else
		data_.erase(data_.end()-count,data_.end());
	return true;
}

bool StringVector::remove(const ConstantSP& index){
	INDEX size = index->size();
	INDEX invSize = data_.size() - size;
	if(invSize <= 0){
		data_.clear();
		containNull_ = false;
		return true;
	}

	INDEX* a[1];
	INDEX** dataSeg = a;
	INDEX segmentSize  = size;
	int segCount = 1;
	if(index->isIndexArray())
		dataSeg[0] = index->getIndexArray();
	else if(index->isHugeIndexArray()){
		dataSeg = index->getHugeIndexArray();
		segmentSize = index->getSegmentSize();
		segCount = (size >> index->getSegmentSizeInBit()) + (size & (segmentSize - 1) ? 1 : 0);
	}
	else
		return false;
	INDEX prevIndex = dataSeg[0][0];
	INDEX cursor = prevIndex;
	INDEX j = 1;

	for(int i=0; i<segCount; ++i){
		INDEX* delIndices = dataSeg[i];
		INDEX count = size - i * segmentSize;
		for(; j<count; ++j){
			if(delIndices[j] > prevIndex + 1){
				INDEX end = delIndices[j];
				for(INDEX k = prevIndex + 1; k<end; ++k)
					data_[cursor++] = data_[k];
			}
			prevIndex = delIndices[j];
		}
		j = 0;
	}

	INDEX total = data_.size();
	for(INDEX k = prevIndex + 1; k<total; ++k)
		data_[cursor++] = data_[k];

	data_.resize(invSize);
	if(containNull_){
		containNull_ = hasNullInRange(0, invSize);
	}
	return true;
}

void StringVector::next(INDEX steps){
	steps=((std::min))(steps,size());
	data_.erase(data_.begin(),data_.begin() + steps);
	data_.insert(data_.end(),steps,"");
	containNull_ = true;
}

void StringVector::prev(INDEX steps){
	INDEX len=size();
	steps=((std::min))(steps,size());
	data_.erase(data_.begin() + (len - steps),data_.end());
	data_.insert(data_.begin(),steps,"");
	containNull_ = true;
}

void StringVector::fill(INDEX start, INDEX length, const ConstantSP& value){
	if(value->isScalar() || length!=value->size()){
		string fillVal=value->getString(0);
		fill_n(data_.begin()+start,length,fillVal);
	}
	else if(value->getCategory()==LITERAL){
		vector<string>::iterator it=data_.begin()+start;
		char* bufVal[Util::BUF_SIZE];
		char** pval;
		INDEX start=0;
		int count;
		while(start<length){
			count=((std::min))(length-start,Util::BUF_SIZE);
			pval=value->getStringConst(start,count,bufVal);
			for(int i=0;i<count;i++)
				*it++=pval[i];
			start+=count;
		}
	}
	else{
		vector<string>::iterator it=data_.begin()+start;
		int count=0;
		while(count<length){
			*it++=value->getString(count++);
		}
	}
	if(value->getNullFlag())
		containNull_=true;
}

void StringVector::nullFill(const ConstantSP& val){
	string replace=val->getString();
	int len=size();
	for(int i=0;i<len;++i)
		if(data_[i]=="")
			data_[i]=replace;
	containNull_=false;
}

bool StringVector::isNull(INDEX start, int len, char* buf) const {
	if(containNull_){
		vector<string>::const_iterator it = data_.begin() + start;
		for(int i=0;i<len;++i){
			buf[i]= it->empty();
			++it;
		}
	}
	else{
		memset(buf,0,len);
	}
	return true;
}

bool StringVector::hasNullInRange(INDEX start, INDEX end){
	vector<string>::const_iterator it = data_.begin() + start;
	for(INDEX i=start; i<end; ++i){
		if(it->empty())
			return true;
		++it;
	}
	return false;
}

int StringVector::serialize(char* buf, int bufSize, INDEX indexStart, int offset, int& numElement, int& partial) const {
    int size_ = size();
    if (indexStart >= size_)
        return -1;
    partial = 0;
    int initialBufSize = bufSize;
    int initialIndex = indexStart;

    if (!blob_) {
        while (bufSize > 0 && indexStart < size_) {
            const string& str = data_[indexStart];
            int len = str.size() + 1 - offset;
            if (bufSize >= len) {
                memcpy(buf, str.c_str() + offset, len);
                buf += len;
                bufSize -= len;
                ++indexStart;
                offset = 0;
            } else {
                memcpy(buf, str.c_str() + offset, bufSize);
                partial = offset + bufSize;
                bufSize = 0;
            }
        }
    } else {
        const int lenBytes = sizeof(int);
        while (bufSize > 0 && indexStart < size_) {
            const string& str = data_[indexStart];
            int len = str.size();
            if (LIKELY(offset == 0)) {
                if (UNLIKELY(bufSize < lenBytes)) {
                    partial = 0;
                    break;
                }
                memcpy(buf, &len, lenBytes);
                buf += lenBytes;
                bufSize -= lenBytes;
            } else {
                offset -= lenBytes;
            }

            if (bufSize >= len - offset) {
                memcpy(buf, str.data() + offset, len - offset);
                buf += len - offset;
                bufSize -= len - offset;
                ++indexStart;
                offset = 0;
            } else {
                memcpy(buf, str.data() + offset, bufSize);
                partial = lenBytes + offset + bufSize;
                bufSize = 0;
            }
        }
    }

    numElement = indexStart - initialIndex;
    return initialBufSize - bufSize;
}

bool StringVector::getString(INDEX start, int len, string** buf) const {
	vector<string>::iterator it=data_.begin()+start;
	for(int i=0;i<len;++i)
		buf[i]=&(*it++);
	return true;
}

bool StringVector::getString(INDEX start, int len, char** buf) const {
	vector<string>::iterator it=data_.begin()+start;
	for(int i=0;i<len;++i)
		buf[i]=(char*)((*it++).c_str());
	return true;
}

string** StringVector::getStringConst(INDEX start, int len, string** buf) const {
	vector<string>::iterator it=data_.begin()+start;
	for(int i=0;i<len;++i)
		buf[i]=&(*it++);
	return buf;
}

char** StringVector::getStringConst(INDEX start, int len, char** buf) const {
	vector<string>::iterator it=data_.begin()+start;
	for(int i=0;i<len;++i)
		buf[i]=(char*)((*it++).c_str());
	return buf;
}

void StringVector::replace(const ConstantSP& oldVal, const ConstantSP& newVal){
	string ov=oldVal->getString(0);
	string nv=newVal->getString(0);
	std::replace(data_.begin(),data_.end(),ov,nv);
}

long long StringVector::getAllocatedMemory() const{
	INDEX size = data_.size();
	long long bytes =sizeof(StringVector)+sizeof(string)*size;
	if(size <= 0)
		return bytes;
	INDEX len= ((std::min))(10, size);
	double sampleBytes = 0;
	for(INDEX i=0;i<len;++i)
		sampleBytes += data_[i].length() + 1;
	return bytes + sampleBytes / len * size;
}

long long StringVector::getAllocatedMemory(INDEX size) const {
	long long bytes =sizeof(StringVector)+sizeof(string)*size;
	if(size <= 0)
		return bytes;
	INDEX len= ((std::min))(10, size);
	double sampleBytes = 0;
	for(INDEX i=0;i<len;++i)
		sampleBytes += data_[i].length() + 1;
	return bytes + sampleBytes / len * size;
}

void AnyVector::clear(){
	data_.clear();
	containNull_ = false;
}

bool AnyVector::set(INDEX index, const ConstantSP& value){
	data_[index]=value;
	value->setIndependent(false);
	value->setTemporary(false);
	if(value->isNull())
		containNull_ = true;
	return true;
}

bool AnyVector::assign(const ConstantSP& value){
	if(value->isVector()){
		if(size()!=value->size())
			return false;
	}
	fill(0,size(),value);
	containNull_ = value->getNullFlag();
	return true;
}

void AnyVector::fill(INDEX start, INDEX length, const ConstantSP& value){
	if(!value->isVector() || length!=value->size()){
		fill_n(data_.begin()+start,length,value);
		if(value->isNull())
			containNull_ = true;
	}
	else{
		deque<ConstantSP>::iterator it=data_.begin()+start;
		int count=0;
		while(count<length){
			*it++=value->get(count++);
		}
		if(value->getNullFlag())
			containNull_ = true;
	}
}

void AnyVector::nullFill(const ConstantSP& val){
	int len=size();
	for(int i=0;i<len;++i)
		if(data_[i]->isNull())
			data_[i]=val;
	containNull_=false;
}

bool AnyVector::isNull(INDEX start, int len, char* buf) const {
	for(int i=0;i<len;++i)
		buf[i]=data_[start+i]->isNull();
	return true;
}

bool AnyVector::isValid(INDEX start, int len, char* buf) const {
	for(int i=0;i<len;++i)
		buf[i]=!data_[start+i]->isNull();
	return true;
}

ConstantSP AnyVector::getValue() const {
	Vector* copy = new AnyVector(data_, containNull_);
	copy->setForm(getForm());
	return copy;
}

char AnyVector::getBool() const {
	if(data_.size() == 1)
		return data_[0]->getBool();
	else
		throw RuntimeException("The any vector can't be converted to bool scalar.");
}

char AnyVector::getChar() const {
	if(data_.size() == 1)
		return data_[0]->getChar();
	else
		throw RuntimeException("The any vector can't be converted to char scalar.");
}

short AnyVector::getShort() const {
	if(data_.size() == 1)
		return data_[0]->getShort();
	else
		throw RuntimeException("The any vector can't be converted to short scalar.");
}

int AnyVector::getInt() const {
	if(data_.size() == 1)
		return data_[0]->getInt();
	else
		throw RuntimeException("The any vector can't be converted to int scalar.");
}

long long AnyVector::getLong() const {
	if(data_.size() == 1)
		return data_[0]->getLong();
	else
		throw RuntimeException("The any vector can't be converted to long scalar.");
}

INDEX AnyVector::getIndex() const {
	if(data_.size() == 1)
		return data_[0]->getIndex();
	else
		throw RuntimeException("The any vector can't be converted to index scalar.");
}

float AnyVector::getFloat() const {
	if(data_.size() == 1)
		return data_[0]->getFloat();
	else
		throw RuntimeException("The any vector can't be converted to float scalar.");
}

double AnyVector::getDouble() const {
	if(data_.size() == 1)
		return data_[0]->getDouble();
	else
		throw RuntimeException("The any vector can't be converted to double scalar.");
}

long long AnyVector::getAllocatedMemory(){
	long long size=sizeof(AnyVector)+sizeof(ConstantSP)*data_.size();
	INDEX len=(INDEX)data_.size();
	for(INDEX i=0;i<len;++i)
		size+=data_[i]->getAllocatedMemory();
	return size;
}

bool AnyVector::hasNull(INDEX start, INDEX len){
	deque<ConstantSP>::const_iterator it = data_.begin() + start;
	for(INDEX i=0; i<len; ++i){
		if((*it)->isNull())
			return true;
		++it;
	}
	return false;
}

bool AnyVector::getBool(INDEX start, int len, char* buf) const{
	deque<ConstantSP>::const_iterator it = data_.begin() + start;
	for(int i=0; i<len; ++i){
		if(!(*it)->isScalar())
			return false;
		buf[i] = (*it)->getBool();
		++it;
	}
	return true;
}

bool AnyVector::getChar(INDEX start, int len,char* buf) const{
	deque<ConstantSP>::const_iterator it = data_.begin() + start;
	for(int i=0; i<len; ++i){
		if(!(*it)->isScalar())
			return false;
		buf[i] = (*it)->getChar();
		++it;
	}
	return true;
}

bool AnyVector::getShort(INDEX start, int len, short* buf) const{
	deque<ConstantSP>::const_iterator it = data_.begin() + start;
	for(int i=0; i<len; ++i){
		if(!(*it)->isScalar())
			return false;
		buf[i] = (*it)->getShort();
		++it;
	}
	return true;
}

bool AnyVector::getInt(INDEX start, int len, int* buf) const{
	deque<ConstantSP>::const_iterator it = data_.begin() + start;
	for(int i=0; i<len; ++i){
		if(!(*it)->isScalar())
			return false;
		buf[i] = (*it)->getInt();
		++it;
	}
	return true;
}

bool AnyVector::getLong(INDEX start, int len, long long* buf) const{
	deque<ConstantSP>::const_iterator it = data_.begin() + start;
	for(int i=0; i<len; ++i){
		if(!(*it)->isScalar())
			return false;
		buf[i] = (*it)->getLong();
		++it;
	}
	return true;
}

bool AnyVector::getIndex(INDEX start, int len, INDEX* buf) const{
	deque<ConstantSP>::const_iterator it = data_.begin() + start;
	for(int i=0; i<len; ++i){
		if(!(*it)->isScalar())
			return false;
		buf[i] = (*it)->getIndex();
		++it;
	}
	return true;
}

bool AnyVector::getFloat(INDEX start, int len, float* buf) const{
	deque<ConstantSP>::const_iterator it = data_.begin() + start;
	for(int i=0; i<len; ++i){
		if(!(*it)->isScalar())
			return false;
		buf[i] = (*it)->getFloat();
		++it;
	}
	return true;
}

bool AnyVector::getDouble(INDEX start, int len, double* buf) const{
	deque<ConstantSP>::const_iterator it = data_.begin() + start;
	for(int i=0; i<len; ++i){
		if(!(*it)->isScalar())
			return false;
		buf[i] = (*it)->getDouble();
		++it;
	}
	return true;
}

const char* AnyVector::getBoolConst(INDEX start, int len, char* buf) const {
	getBool(start, len, buf);
	return buf;
}

const char* AnyVector::getCharConst(INDEX start, int len,char* buf) const {
	getChar(start, len, buf);
	return buf;
}

const short* AnyVector::getShortConst(INDEX start, int len, short* buf) const {
	getShort(start, len, buf);
	return buf;
}

const int* AnyVector::getIntConst(INDEX start, int len, int* buf) const {
	getInt(start, len, buf);
	return buf;
}

const long long* AnyVector::getLongConst(INDEX start, int len, long long* buf) const {
	getLong(start, len, buf);
	return buf;
}

const INDEX* AnyVector::getIndexConst(INDEX start, int len, INDEX* buf) const {
	getIndex(start, len, buf);
	return buf;
}

const float* AnyVector::getFloatConst(INDEX start, int len, float* buf) const {
	getFloat(start, len, buf);
	return buf;
}

const double* AnyVector::getDoubleConst(INDEX start, int len, double* buf) const {
	getDouble(start, len, buf);
	return buf;
}

bool AnyVector::set(const ConstantSP& index, const ConstantSP& value){
	if(index->isVector()){
		INDEX len=index->size();
		INDEX bufIndex[Util::BUF_SIZE];
		const INDEX* pindex;
		INDEX start=0;
		int count;
		while(start<len){
			count=((std::min))(len-start,Util::BUF_SIZE);
			pindex=index->getIndexConst(start,count,bufIndex);
			for(int i=0;i<count;i++){
				ConstantSP obj = value->get(start + i);
				obj->setIndependent(false);
				obj->setTemporary(false);
				data_[pindex[i]] = obj;
			}
			start+=count;
		}
		if(value->getNullFlag())
			containNull_=true;
	}
	else{
		data_[index->getIndex()] = value;
		if(value->isNull())
			containNull_=true;
		value->setIndependent(false);
		value->setTemporary(false);
	}
	return true;
}

ConstantSP AnyVector::getSubVector(INDEX start, INDEX length) const {
	AnyVector* vec=new AnyVector((INDEX)0);
	ConstantSP result(vec);
	if(start<0 || start>=size() || length>size())
		return result;

	if(length>0)
		vec->data_.insert(vec->data_.begin(),data_.begin()+start,data_.begin()+(start+length));
	else
		vec->data_.insert(vec->data_.begin(),data_.rbegin()+(size()-1-start),data_.rbegin()+(size()-1-start+length));
	result->setNullFlag(containNull_);
	return result;
}

ConstantSP AnyVector::get(const ConstantSP& index) const {
	UINDEX size=data_.size();
	if(index->isVector()){
		INDEX len=index->size();
		ConstantSP result = Util::createVector(DT_ANY, len);
		Constant* p = result.get();
		if(index->isIndexArray()){
			UINDEX* bufIndex=(UINDEX*)index->getIndexArray();
			for(int i=0;i<len;++i)
				p->set(i, bufIndex[i]<size ? data_[bufIndex[i]] : Constant::void_);
		}
		else{
			const int bufSize=Util::BUF_SIZE;
			UINDEX bufIndex[bufSize];
			INDEX start=0;
			int count=0;
			int i;
			while(start<len){
				count=((std::min))(len-start,bufSize);
				index->getIndex(start,count,(INDEX*)bufIndex);
				for(i=0;i<count;i++)
					p->set(start+i, bufIndex[i]<size ? data_[bufIndex[i]] : Constant::void_);
				start+=count;
			}
		}
		p->setNullFlag(containNull_ || p->hasNull());
		return result;
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return idx<size ? data_[idx] : Constant::void_;
	}
}

bool AnyVector::append(const ConstantSP& value){
	if(data_.size() >= (UINDEX)Util::MAX_LENGTH_FOR_ANY_VECTOR)
		return false;
	value->setIndependent(false);
	value->setTemporary(false);
	data_.push_back(value);
	if(value->isNull())
		containNull_=true;
	return true;
}

bool AnyVector::remove(INDEX count){
	bool fromHead=(count<0);
	count=((std::min))(size(),abs(count));
	if(fromHead)
		data_.erase(data_.begin(),data_.begin()+count);
	else
		data_.erase(data_.end()-count,data_.end());
	return true;
}

void AnyVector::next(INDEX steps){
	steps=((std::min))(steps,size());
	data_.insert(data_.end(),steps,Constant::void_);
	setNullFlag(true);
	data_.erase(data_.begin(), data_.begin()+steps);
}

void AnyVector::prev(INDEX steps){
	int len=size();
	steps=((std::min))(steps,size());
	data_.insert(data_.begin(),steps,Constant::void_);
	setNullFlag(true);
	data_.erase(data_.begin()+len, data_.end());
}

ConstantSP AnyVector::convertToRegularVector() const {
	DATA_TYPE type;
	if(!isHomogeneousScalar(type))
		return Constant::void_;
	VectorSP tmp = Util::createVector(type, data_.size());
	deque<ConstantSP>::const_iterator it = data_.begin();
	deque<ConstantSP>::const_iterator end = data_.end();
	int cursor = 0;
	while(it != end){
		tmp->set(cursor++, *it++);
	}
	return tmp;
}

bool AnyVector::isHomogeneousScalar(DATA_TYPE& type) const {
	if(data_.empty() || !data_[0]->isScalar())
		return false;
	type = data_[0]->getType();
	deque<ConstantSP>::const_iterator it = data_.begin();
	deque<ConstantSP>::const_iterator end = data_.end();
	while(++it != end){
		const ConstantSP& cur = *it;
		if(!cur->isScalar() || cur->getType() != type)
			return false;
	}
	return true;
}

bool AnyVector::isTabular() const {
	if(data_.empty())
		return false;
	deque<ConstantSP>::const_iterator it = data_.begin();
	deque<ConstantSP>::const_iterator end = data_.end();
	while(it != end){
		const ConstantSP& cur = *it++;
		if(!cur->isArray() || cur->getType() == DT_ANY)
			return false;
	}
	return true;
}

bool AnyVector::containNotMarshallableObject() const {
	deque<ConstantSP>::const_iterator  it = data_.begin();
	deque<ConstantSP>::const_iterator  end = data_.end();
	while(it != end){
		if((*it)->containNotMarshallableObject())
			return true;
		++it;
	}
	return false;
}

bool FastBoolVector::set(INDEX index, const ConstantSP& value){
	data_[index]=value->getBool();
	if(data_[index]==nullVal_)
		containNull_=true;
	return true;
}

ConstantSP FastBoolVector::get(INDEX index) const {
	return ConstantSP(new Bool(data_[index]));
}

ConstantSP FastBoolVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new Bool(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

void FastBoolVector::fill(INDEX start, INDEX length, const ConstantSP& value){
	if(value->size()==1 || value->size()!=length){
		char fillVal=value->getBool();
		int end=start+length;
		for(int i=start;i<end;++i)
			data_[i]=fillVal;
	}
	else if(!value->getBool(0,length,data_+start))
		throw RuntimeException("Failed to read bool data from the given vector.");
	if(value->getNullFlag())
		containNull_=true;
}

bool FastBoolVector::append(const ConstantSP& value, INDEX appendSize){
	if(!checkCapacity(appendSize))
		return false;

	if(appendSize==1)
		data_[size_] = value->getBool(0);
	else if(!value->getBool(0,appendSize,data_+size_))
		return false;
	size_+=appendSize;
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

int FastBoolVector::compare(INDEX index, const ConstantSP& target) const {
	char val=target->getBool();
	return data_[index]==val?0:(data_[index]<val?-1:1);
}

bool FastBoolVector::set(const ConstantSP& index, const ConstantSP& value){
	if(index->isVector()){
		INDEX len=index->size();
		INDEX bufIndex[Util::BUF_SIZE];
		char bufVal[Util::BUF_SIZE];
		const INDEX* pindex;
		const char* pval;
		INDEX start=0;
		int count;
		while(start<len){
			count=((std::min))(len-start,Util::BUF_SIZE);
			pindex=index->getIndexConst(start,count,bufIndex);
			pval=value->getBoolConst(start,count,bufVal);
			for(int i=0;i<count;i++)
				data_[pindex[i]]=pval[i];
			start+=count;
		}
	}
	else
		data_[index->getIndex()]=value->getBool();
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

bool FastCharVector::set(INDEX index, const ConstantSP& value){
	data_[index]=value->getChar();
	if(data_[index]==nullVal_)
		containNull_=true;
	return true;
}

ConstantSP FastCharVector::get(INDEX index) const {
	return ConstantSP( new Char(data_[index]));
}

ConstantSP FastCharVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new Char(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

void FastCharVector::fill(INDEX start, INDEX length, const ConstantSP& value){
	if(value->size()==1 || value->size()!=length){
		char fillVal=value->getChar();
		int end=start+length;
		for(int i=start;i<end;++i)
			data_[i]=fillVal;
	}
	else if(!value->getChar(0,length,data_+start))
		throw RuntimeException("Failed to read char data from the given vector.");
	if(value->getNullFlag())
		containNull_=true;
}

bool FastCharVector::append(const ConstantSP& value, INDEX appendSize){
	if(!checkCapacity(appendSize))
		return false;

	if(appendSize==1)
		data_[size_] = value->getChar(0);
	else if(!value->getChar(0,appendSize,data_+size_))
		return false;
	size_+=appendSize;
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

bool FastCharVector::validIndex(INDEX uplimit){
	return validIndex(0, size_, uplimit);
}

bool FastCharVector::validIndex(INDEX start, INDEX length, INDEX uplimit){
	unsigned char limit=(unsigned char)((std::min))(uplimit,(int)CHAR_MAX);
	unsigned char* data=(unsigned char*)data_;
	INDEX end = start + length;
	for(INDEX i=start;i<end;++i)
		if(data[i]>limit)
			return false;
	return true;
}

int FastCharVector::compare(INDEX index, const ConstantSP& target) const {
	char val=target->getChar();
	return data_[index]==val?0:(data_[index]<val?-1:1);
}

void FastCharVector::upper(){
	for(INDEX i=0; i<size_; ++i){
		if(data_[i] >= 'a' && data_[i] <= 'z')
			data_[i] -= 32;
	}
}

void FastCharVector::lower(){
	for(INDEX i=0; i<size_; ++i){
		if(data_[i] >= 'A' && data_[i] <= 'Z')
			data_[i] += 32;
	}
}

bool FastCharVector::set(const ConstantSP& index, const ConstantSP& value){
	if(index->isVector()){
		INDEX len=index->size();
		INDEX bufIndex[Util::BUF_SIZE];
		char bufVal[Util::BUF_SIZE];
		const INDEX* pindex;
		const char* pval;
		INDEX start=0;
		int count;
		while(start<len){
			count=((std::min))(len-start,Util::BUF_SIZE);
			pindex=index->getIndexConst(start,count,bufIndex);
			pval=value->getCharConst(start,count,bufVal);
			for(int i=0;i<count;i++)
				data_[pindex[i]]=pval[i];
			start+=count;
		}
	}
	else
		data_[index->getIndex()]=value->getChar();
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

bool FastShortVector::set(INDEX index, const ConstantSP& value){
	data_[index]=value->getShort();
	if(data_[index]==nullVal_)
		containNull_=true;
	return true;
}

ConstantSP FastShortVector::get(INDEX index) const {
	return ConstantSP(new Short(data_[index]));
}

ConstantSP FastShortVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new Short(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

void FastShortVector::fill(INDEX start, INDEX length, const ConstantSP& value){
	if(value->size()==1 || value->size()!=length){
		short fillVal=value->getShort();
		int end=start+length;
		for(int i=start;i<end;++i)
			data_[i]=fillVal;
	}
	else if(!value->getShort(0,length,data_+start))
		throw RuntimeException("Failed to read short data from the given vector.");
	if(value->getNullFlag())
		containNull_=true;
}

bool FastShortVector::append(const ConstantSP& value, INDEX appendSize){
	if(!checkCapacity(appendSize))
		return false;

	if(appendSize==1)
		data_[size_] = value->getShort(0);
	else if(!value->getShort(0,appendSize,data_+size_))
		return false;
	size_+=appendSize;
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

bool FastShortVector::validIndex(INDEX uplimit){
	return validIndex(0, size_, uplimit);
}

bool FastShortVector::validIndex(INDEX start, INDEX length, INDEX uplimit){
	unsigned short limit=(unsigned short)((std::min))(uplimit,(int)SHRT_MAX);
	unsigned short* data=(unsigned short*)data_;
	INDEX end = start + length;
	for(INDEX i=start;i<end;++i)
		if(data[i]>limit)
			return false;
	return true;
}

int FastShortVector::compare(INDEX index, const ConstantSP& target) const {
	short val=target->getShort();
	return data_[index]==val?0:(data_[index]<val?-1:1);
}

bool FastShortVector::set(const ConstantSP& index, const ConstantSP& value){
	if(index->isVector()){
		INDEX len=index->size();
		INDEX bufIndex[Util::BUF_SIZE];
		short bufVal[Util::BUF_SIZE];
		const INDEX* pindex;
		const short* pval;
		INDEX start=0;
		int count;
		while(start<len){
			count=((std::min))(len-start,Util::BUF_SIZE);
			pindex=index->getIndexConst(start,count,bufIndex);
			pval=value->getShortConst(start,count,bufVal);
			for(int i=0;i<count;i++)
				data_[pindex[i]]=pval[i];
			start+=count;
		}
	}
	else
		data_[index->getIndex()]=value->getShort();
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

bool FastIntVector::set(INDEX index, const ConstantSP& value){
	data_[index]=value->getInt();
	if(data_[index]==nullVal_)
		containNull_=true;
	return true;
}

ConstantSP  FastIntVector::get(INDEX index) const {
	return ConstantSP(new Int(data_[index]));
}

ConstantSP  FastIntVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new Int(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

void  FastIntVector::fill(INDEX start, INDEX length, const ConstantSP& value){
	if(value->size()==1 || value->size()!=length){
		int fillVal=value->getInt();
		int end=start+length;
		for(int i=start;i<end;++i)
			data_[i]=fillVal;
	}
	else if(!value->getInt(0,length,data_+start))
		throw RuntimeException("Failed to read int data from the given vector.");
	if(value->getNullFlag())
		containNull_=true;
}

bool  FastIntVector::append(const ConstantSP& value, INDEX appendSize){
	if(!checkCapacity(appendSize))
		return false;

	if(appendSize==1)
		data_[size_] = value->getInt(0);
	else if(!value->getInt(0,appendSize,data_+size_))
		return false;
	size_+=appendSize;
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

bool  FastIntVector::validIndex(INDEX uplimit){
	return validIndex(0, size_, uplimit);
}

bool  FastIntVector::validIndex(INDEX start, INDEX length, INDEX uplimit){
	unsigned int limit=(unsigned int)uplimit;
	unsigned int* data=(unsigned int*)data_;
	INDEX end = start + length;
	for(INDEX i=start;i<end;++i)
		if(data[i]>limit)
			return false;
	return true;
}

int  FastIntVector::compare(INDEX index, const ConstantSP& target) const {
	int val=target->getInt();
	return data_[index]==val?0:(data_[index]<val?-1:1);
}

bool FastIntVector::set(const ConstantSP& index, const ConstantSP& value){
	if(index->isVector()){
		INDEX len=index->size();
		INDEX bufIndex[Util::BUF_SIZE];
		int bufVal[Util::BUF_SIZE];
		const INDEX* pindex;
		const int* pval;
		INDEX start=0;
		int count;
		while(start<len){
			count=((std::min))(len-start,Util::BUF_SIZE);
			pindex=index->getIndexConst(start,count,bufIndex);
			pval=value->getIntConst(start,count,bufVal);
			for(int i=0;i<count;i++)
				data_[pindex[i]]=pval[i];
			start+=count;
		}
	}
	else
		data_[index->getIndex()]=value->getInt();
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

bool FastLongVector::set(INDEX index, const ConstantSP& value){
	data_[index]=value->getLong();
	if(data_[index]==nullVal_)
		containNull_=true;
	return true;
}

ConstantSP FastLongVector::get(INDEX index) const {
	return ConstantSP(new Long(data_[index]));
}

ConstantSP FastLongVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new Long(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

void FastLongVector::fill(INDEX start, INDEX length, const ConstantSP& value){
	if(value->size()==1 || value->size()!=length){
		long long fillVal=value->getLong();
		int end=start+length;
		for(int i=start;i<end;++i)
			data_[i]=fillVal;
	}
	else if(!value->getLong(0,length,data_+start))
		throw RuntimeException("Failed to read long data from the given vector.");
	if(value->getNullFlag())
		containNull_=true;
}

bool FastLongVector::append(const ConstantSP& value, INDEX appendSize){
	if(!checkCapacity(appendSize))
		return false;

	if(appendSize==1)
		data_[size_] = value->getLong(0);
	else if(!value->getLong(0,appendSize,data_+size_))
		return false;
	size_+=appendSize;
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

bool FastLongVector::validIndex(INDEX uplimit){
	return validIndex(0, size_, uplimit);
}

bool FastLongVector::validIndex(INDEX start, INDEX length, INDEX uplimit){
	unsigned long limit=uplimit;
	unsigned long* data=(unsigned long*)data_;
	INDEX end = start + length;
	for(INDEX i=start;i<end;++i)
		if(data[i]>limit)
			return false;
	return true;
}

int FastLongVector::compare(INDEX index, const ConstantSP& target) const {
	long long targetVal=target->getLong();
	if(data_[index]==targetVal)
		return 0;
	else if(data_[index]<targetVal)
		return -1;
	else
		return 1;
}

bool FastLongVector::set(const ConstantSP& index, const ConstantSP& value){
	if(index->isVector()){
		INDEX len=index->size();
		INDEX bufIndex[Util::BUF_SIZE];
		long long bufVal[Util::BUF_SIZE];
		const INDEX* pindex;
		const long long* pval;
		INDEX start=0;
		int count;
		while(start<len){
			count=((std::min))(len-start,Util::BUF_SIZE);
			pindex=index->getIndexConst(start,count,bufIndex);
			pval=value->getLongConst(start,count,bufVal);
			for(int i=0;i<count;i++)
				data_[pindex[i]]=pval[i];
			start+=count;
		}
	}
	else
		data_[index->getIndex()]=value->getLong();
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

bool FastFloatVector::set(INDEX index, const ConstantSP& value){
	data_[index]=value->getFloat();
	if(data_[index]==nullVal_)
		containNull_=true;
	return true;
}

ConstantSP FastFloatVector::get(INDEX index) const {
	return ConstantSP(new Float(data_[index]));
}

ConstantSP FastFloatVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new Float(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

void FastFloatVector::fill(INDEX start, INDEX length, const ConstantSP& value){
	if(value->size()==1 || value->size()!=length){
		float fillVal=value->getFloat();
		int end=start+length;
		for(int i=start;i<end;++i)
			data_[i]=fillVal;
	}
	else if(!value->getFloat(0,length,data_+start))
		throw RuntimeException("Failed to read float data from the given vector.");
	if(value->getNullFlag())
		containNull_=true;
}

bool FastFloatVector::append(const ConstantSP& value, INDEX appendSize){
	if(!checkCapacity(appendSize))
		return false;

	if(appendSize==1)
		data_[size_] = value->getFloat(0);
	else if(!value->getFloat(0,appendSize,data_+size_))
		return false;
	size_+=appendSize;
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

int FastFloatVector::compare(INDEX index, const ConstantSP& target) const {
	float targetVal=target->getFloat();
	if(data_[index]==targetVal)
		return 0;
	else if(data_[index]<targetVal)
		return -1;
	else
		return 1;
}

bool FastFloatVector::set(const ConstantSP& index, const ConstantSP& value){
	if(index->isVector()){
		INDEX len=index->size();
		INDEX bufIndex[Util::BUF_SIZE];
		float bufVal[Util::BUF_SIZE];
		const INDEX* pindex;
		const float* pval;
		INDEX start=0;
		int count;
		while(start<len){
			count=((std::min))(len-start,Util::BUF_SIZE);
			pindex=index->getIndexConst(start,count,bufIndex);
			pval=value->getFloatConst(start,count,bufVal);
			for(int i=0;i<count;i++)
				data_[pindex[i]]=pval[i];
			start+=count;
		}
	}
	else
		data_[index->getIndex()]=value->getFloat();
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

bool FastDoubleVector::set(INDEX index, const ConstantSP& value){
	data_[index]=value->getDouble();
	if(data_[index]==nullVal_)
		containNull_=true;
	return true;
}

ConstantSP FastDoubleVector::get(INDEX index) const {
	return ConstantSP(new Double(data_[index]));
}

ConstantSP FastDoubleVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new Double(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

void FastDoubleVector::fill(INDEX start, INDEX length, const ConstantSP& value){
	if(value->size()==1 || value->size()!=length){
		double fillVal=value->getDouble();
		int end=start+length;
		for(int i=start;i<end;++i)
			data_[i]=fillVal;
	}
	else if(!value->getDouble(0,length,data_+start))
		throw RuntimeException("Failed to read double data from the given vector.");
	if(value->getNullFlag())
		containNull_=true;
}

bool FastDoubleVector::append(const ConstantSP& value, INDEX appendSize){
	if(!checkCapacity(appendSize))
		return false;
	if(appendSize==1)
		data_[size_] = value->getDouble(0);
	else if(!value->getDouble(0,appendSize,data_+size_))
		return false;
	size_+=appendSize;
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

int FastDoubleVector::compare(INDEX index, const ConstantSP& target) const {
	double targetVal=target->getDouble();
	if(data_[index]==targetVal)
		return 0;
	else if(data_[index]<targetVal)
		return -1;
	else
		return 1;
}

bool FastDoubleVector::set(const ConstantSP& index, const ConstantSP& value){
	if(index->isVector()){
		INDEX len=index->size();
		INDEX bufIndex[Util::BUF_SIZE];
		double bufVal[Util::BUF_SIZE];
		const INDEX* pindex;
		const double* pval;
		INDEX start=0;
		int count;
		while(start<len){
			count=((std::min))(len-start,Util::BUF_SIZE);
			pindex=index->getIndexConst(start,count,bufIndex);
			pval=value->getDoubleConst(start,count,bufVal);
			for(int i=0;i<count;i++)
				data_[pindex[i]]=pval[i];
			start+=count;
		}
	}
	else
		data_[index->getIndex()]=value->getDouble();
	if(value->getNullFlag())
		containNull_=true;
	return true;
}

ConstantSP FastDateVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new Date(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

ConstantSP FastDateVector::castTemporal(DATA_TYPE expectType){
	if(expectType != DT_DATEHOUR && (expectType < DT_DATE || expectType > DT_NANOTIMESTAMP)){
		throw RuntimeException("castTemporal from DATE to "+ Util::getDataTypeString(expectType)+" not supported ");
	}
	if(expectType != DT_DATE && expectType != DT_TIMESTAMP && expectType != DT_NANOTIMESTAMP && expectType != DT_MONTH && expectType != DT_DATETIME && expectType != DT_DATEHOUR){
		throw RuntimeException("castTemporal from DATE to "+ Util::getDataTypeString(expectType)+" not supported ");
	}
	if(expectType == DT_DATE)
		return getValue();

	VectorSP res = Util::createVector(expectType, size_);
	if(expectType == DT_DATEHOUR){
        int *pbuf = (int*)res->getDataArray();
        for(int i = 0; i < size_; i++){
            data_[i] == INT_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] * 24;
        }
        return res;
	}
	long long ratio = Util::getTemporalConversionRatio(DT_DATE, expectType);
	if(expectType == DT_NANOTIMESTAMP ||  expectType  == DT_TIMESTAMP){
		long long *pbuf = (long long*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			data_[i] == INT_MIN ? pbuf[i] = LLONG_MIN : pbuf[i] = (long long)data_[i] * ratio;
		}
	}
	else if(expectType == DT_DATETIME){
		int *pbuf = (int*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			data_[i] == INT_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] * ratio;
		}
	}
	else{
		int *pbuf = (int*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			if(data_[i] == INT_MIN){
				pbuf[i] = INT_MIN;
				continue;
			}
			int year, month, day;
			Util::parseDate(data_[i], year, month, day);
			pbuf[i] = year*12+month-1;
		}
	}
	return res;
}

ConstantSP FastDateTimeVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new DateTime(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

ConstantSP FastDateTimeVector::castTemporal(DATA_TYPE expectType){
	if(expectType != DT_DATEHOUR && (expectType < DT_DATE || expectType > DT_NANOTIMESTAMP)){
		throw RuntimeException("castTemporal from DATETIME to "+ Util::getDataTypeString(expectType)+" not supported ");
	}
	if(expectType == DT_DATETIME)
		return getValue();

	VectorSP res = Util::createVector(expectType, size_);
	if(expectType == DT_DATEHOUR){
        int *pbuf = (int*)res->getDataArray();
        for(int i = 0; i < size_; i++){
			int tail = (data_[i] < 0) && (data_[i] % 3600);
            data_[i] == INT_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] / 3600 - tail;
        }
        return res;
	}
	long long ratio = Util::getTemporalConversionRatio(DT_DATETIME, expectType);
	if(expectType == DT_NANOTIMESTAMP ||  expectType  == DT_TIMESTAMP){
		long long *pbuf = (long long*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			data_[i] == INT_MIN ? pbuf[i] = LLONG_MIN : pbuf[i] = (long long)data_[i] * ratio;
		}
	}
	else if(expectType == DT_DATE){
		int *pbuf = (int*)res->getDataArray();
		ratio = -ratio;
		for(int i = 0; i < size_; i++){
			int tail = (data_[i] < 0) && (data_[i] % ratio);
			data_[i] == INT_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] / ratio - tail;
		}
	}
	else if(expectType == DT_MONTH){
		int *pbuf = (int*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			if(data_[i] == INT_MIN){
				pbuf[i] = INT_MIN;
				continue;
			}
			int year, month, day;
			Util::parseDate(data_[i] / 86400, year, month, day);
			pbuf[i] = year*12+month-1;
		}
	}
	else if(expectType == DT_NANOTIME){
		long long *pbuf = (long long*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			int remainder = data_[i] % 86400;
			data_[i] == INT_MIN ? pbuf[i] = LLONG_MIN : pbuf[i] = (long long)(remainder + ((data_[i] < 0) && remainder) * 86400) * 1000000000LL;
		}
	}
	else{
		ratio = Util::getTemporalConversionRatio(DT_SECOND, expectType);
		int *pbuf = (int*)res->getDataArray();
		if(ratio > 0){
			for(int i = 0; i < size_; i++){
				int remainder = data_[i] % 86400;
				data_[i] == INT_MIN ? pbuf[i] = INT_MIN : pbuf[i] = (remainder + ((data_[i] < 0) && remainder) * 86400) * ratio;
			}
		}
		else{
			ratio = -ratio;
			for(int i = 0; i < size_; i++){
				int remainder = data_[i] % 86400;
				data_[i] == INT_MIN ? pbuf[i] = INT_MIN : pbuf[i] = (remainder + ((data_[i] < 0) && remainder) * 86400) / ratio;
			}
		}
	}
	return res;
}

ConstantSP FastDateHourVector::get(const ConstantSP& index) const {
    if(index->isVector()){
        return retrieve((Vector*)index.get());
    }
    else{
        UINDEX idx=(UINDEX)index->getIndex();
        return ConstantSP(new DateHour(idx<(UINDEX)size_?data_[idx]:nullVal_));
    }
}

ConstantSP FastDateHourVector::castTemporal(DATA_TYPE expectType){
    if(expectType != DT_DATEHOUR && (expectType < DT_DATE || expectType > DT_NANOTIMESTAMP)){
        throw RuntimeException("castTemporal from DATEHOUR to "+ Util::getDataTypeString(expectType)+" not supported ");
    }
    if(expectType == DT_DATEHOUR)
        return getValue();

    VectorSP res = Util::createVector(expectType, size_);
    long long ratio = Util::getTemporalConversionRatio(DT_DATETIME, expectType);
    if(expectType == DT_NANOTIMESTAMP ||  expectType  == DT_TIMESTAMP){
        long long *pbuf = (long long*)res->getDataArray();
        for(int i = 0; i < size_; i++){
            data_[i] == INT_MIN ? pbuf[i] = LLONG_MIN : pbuf[i] = (long long)data_[i] * ratio * 3600;
        }
    }
    else if(expectType == DT_DATETIME){
        int *pbuf = (int*)res->getDataArray();
        for(int i = 0; i < size_; i++){
            data_[i] == INT_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] * 3600;
        }
    }
    else if(expectType == DT_DATE){
        int *pbuf = (int*)res->getDataArray();
        for(int i = 0; i < size_; i++){
            data_[i] == INT_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] * 3600 / (-ratio) ;
        }
    }
    else if(expectType == DT_MONTH){
        int *pbuf = (int*)res->getDataArray();
        for(int i = 0; i < size_; i++){
            if(data_[i] == INT_MIN){
                pbuf[i] = INT_MIN;
                continue;
            }
            int year, month, day;
            Util::parseDate(data_[i] * 3600 / 86400, year, month, day);
            pbuf[i] = year*12+month-1;
        }
    }
    else if(expectType == DT_NANOTIME){
        long long *pbuf = (long long*)res->getDataArray();
        for(int i = 0; i < size_; i++){
            data_[i] == INT_MIN ? pbuf[i] = LLONG_MIN : pbuf[i] = (long long)data_[i] * 3600 % 86400 * 1000000000LL;
        }
    }
    else{
        ratio = Util::getTemporalConversionRatio(DT_SECOND, expectType);
        int *pbuf = (int*)res->getDataArray();
        if(ratio > 0){
            for(int i = 0; i < size_; i++){
                data_[i] == INT_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] * 3600 % 86400 * ratio;
            }
        }
        else{
            for(int i = 0; i < size_; i++){
                data_[i] == INT_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] * 3600 % 86400 / (-ratio);
            }
        }
    }
    return res;
}

ConstantSP FastMonthVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new Month(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

ConstantSP FastTimeVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new Time(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

void FastTimeVector::validate(){
	unsigned int* myData = (unsigned int*)data_;
	for(INDEX i=0; i<size_; ++i){
		if(myData[i] >= 86400){
			data_[i] = INT_MIN;
			containNull_ = true;
		}
	}
}

ConstantSP FastTimeVector::castTemporal(DATA_TYPE expectType){
	if(expectType < DT_DATE || expectType > DT_NANOTIMESTAMP){
		throw RuntimeException("castTemporal from TIME to "+ Util::getDataTypeString(expectType)+" not supported ");
	}
	if(expectType != DT_TIME && expectType != DT_NANOTIME && expectType != DT_SECOND && expectType != DT_MINUTE){
		throw RuntimeException("castTemporal from TIME to "+ Util::getDataTypeString(expectType)+" not supported ");
	}
	if(expectType == DT_TIME)
		return getValue();

	VectorSP res = Util::createVector(expectType, size_);
	long long ratio = Util::getTemporalConversionRatio(DT_TIME, expectType);
	if(expectType == DT_NANOTIME){
		long long *pbuf = (long long*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			data_[i] == INT_MIN ? pbuf[i] = LLONG_MIN : pbuf[i] = (long long)data_[i] * ratio;
		}
	}
	else{
		int *pbuf = (int*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			data_[i] == INT_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] / (-ratio);
		}
	}
	return res;
}

ConstantSP FastMinuteVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new Minute(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

void FastMinuteVector::validate(){
	unsigned int* myData = (unsigned int*)data_;
	for(INDEX i=0; i<size_; ++i){
		if(myData[i] >= 1440){
			data_[i] = INT_MIN;
			containNull_ = true;
		}
	}
}

ConstantSP FastMinuteVector::castTemporal(DATA_TYPE expectType){
	if(expectType < DT_DATE || expectType > DT_NANOTIMESTAMP){
		throw RuntimeException("castTemporal from MINUTE to "+ Util::getDataTypeString(expectType)+" not supported ");
	}
	if(expectType != DT_TIME && expectType != DT_NANOTIME && expectType != DT_SECOND && expectType != DT_MINUTE){
		throw RuntimeException("castTemporal from MINUTE to "+ Util::getDataTypeString(expectType)+" not supported ");
	}
	if(expectType == DT_MINUTE)
		return getValue();

	VectorSP res = Util::createVector(expectType, size_);
	long long ratio = Util::getTemporalConversionRatio(DT_MINUTE, expectType);
	if(expectType == DT_NANOTIME){
		long long *pbuf = (long long*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			data_[i] == INT_MIN ? pbuf[i] = LLONG_MIN : pbuf[i] = (long long)data_[i] * ratio;
		}
	}
	else{
		int *pbuf = (int*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			data_[i] == INT_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] * ratio;
		}
	}
	return res;
}

ConstantSP FastSecondVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new Second(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

void FastSecondVector::validate(){
	unsigned int* myData = (unsigned int*)data_;
	for(INDEX i=0; i<size_; ++i){
		if(myData[i] >= 86400){
			data_[i] = INT_MIN;
			containNull_ = true;
		}
	}
}

ConstantSP FastSecondVector::castTemporal(DATA_TYPE expectType){
	if(expectType < DT_DATE || expectType > DT_NANOTIMESTAMP){
		throw RuntimeException("castTemporal from SECOND to "+ Util::getDataTypeString(expectType)+" not supported ");
	}
	if(expectType != DT_TIME && expectType != DT_NANOTIME && expectType != DT_SECOND && expectType != DT_MINUTE){
		throw RuntimeException("castTemporal from SECOND to "+ Util::getDataTypeString(expectType)+" not supported ");
	}
	if(expectType == DT_SECOND)
		return getValue();

	VectorSP res = Util::createVector(expectType, size_);
	long long ratio = Util::getTemporalConversionRatio(DT_SECOND, expectType);
	if(expectType == DT_NANOTIME){
		long long *pbuf = (long long*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			data_[i] == INT_MIN ? pbuf[i] = LLONG_MIN : pbuf[i] = data_[i] * ratio;
		}
	}
	else if(expectType == DT_TIME){
		int *pbuf = (int*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			data_[i] == INT_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] * ratio;
		}
	}
	else{
		int *pbuf = (int*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			data_[i] == INT_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] / (-ratio);
		}
	}
	return res;
}

ConstantSP FastNanoTimeVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new NanoTime(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

void FastNanoTimeVector::validate(){
	unsigned long long* myData = (unsigned long long*)data_;
	for(INDEX i=0; i<size_; ++i){
		if(myData[i] >= 86400000000000ll){
			data_[i] = LLONG_MIN;
			containNull_ = true;
		}
	}
}

ConstantSP FastNanoTimeVector::castTemporal(DATA_TYPE expectType){
	if(expectType < DT_DATE || expectType > DT_NANOTIMESTAMP){
		throw RuntimeException("castTemporal from NANOTIME to "+ Util::getDataTypeString(expectType)+" not supported ");
	}
	if(expectType != DT_TIME && expectType != DT_NANOTIME && expectType != DT_SECOND && expectType != DT_MINUTE){
		throw RuntimeException("castTemporal from NANOTIME to "+ Util::getDataTypeString(expectType)+" not supported ");
	}
	if(expectType == DT_NANOTIME)
		return getValue();

	VectorSP res = Util::createVector(expectType, size_);
	long long ratio = Util::getTemporalConversionRatio(DT_NANOTIME, expectType);

	int *pbuf = (int*)res->getDataArray();
	for(int i = 0; i < size_; i++){
		data_[i] == LLONG_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] / (-ratio);
	}
	return res;
}

ConstantSP FastTimestampVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new Timestamp(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

ConstantSP FastTimestampVector::castTemporal(DATA_TYPE expectType){
	if(expectType != DT_DATEHOUR && (expectType < DT_DATE || expectType > DT_NANOTIMESTAMP)){
		throw RuntimeException("castTemporal from TIMESTAMP to "+ Util::getDataTypeString(expectType)+" not supported ");
	}
	if(expectType == DT_TIMESTAMP)
		return getValue();

	VectorSP res = Util::createVector(expectType, size_);
	if(expectType == DT_DATEHOUR){
        int *pbuf = (int*)res->getDataArray();
        for(int i = 0; i < size_; i++){
			int tail = (data_[i] < 0) && (data_[i] % 3600);
            data_[i] == LLONG_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] / 3600000LL - tail;
        }
        return res;
	}
	long long ratio = Util::getTemporalConversionRatio(DT_TIMESTAMP, expectType);
	if(expectType == DT_NANOTIMESTAMP){
		long long *pbuf = (long long*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			data_[i] == LLONG_MIN ? pbuf[i] = LLONG_MIN : pbuf[i] = data_[i] * ratio;
		}
	}
	else if(expectType == DT_DATE || expectType == DT_DATETIME){
		int *pbuf = (int*)res->getDataArray();
		ratio = -ratio;
		for(int i = 0; i < size_; i++){
			int tail = (data_[i] < 0) && (data_[i] % ratio);
			data_[i] == LLONG_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] / ratio - tail;		
		}
	}
	else if(expectType == DT_MONTH){
		int *pbuf = (int*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			if(data_[i] == LLONG_MIN){
				pbuf[i] = INT_MIN;
				continue;
			}
			int year, month, day;
			Util::parseDate(data_[i] / 86400000, year, month, day);
			pbuf[i] = year*12+month-1;
		}
	}
	else if(expectType == DT_NANOTIME){
		long long *pbuf = (long long*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			int remainder = data_[i] % 86400000;
			data_[i] == LLONG_MIN ? pbuf[i] = LLONG_MIN : pbuf[i] = (remainder + ((data_[i] < 0) && remainder) * 86400000) * 1000000ll;
		}
	}
	else{
		ratio = Util::getTemporalConversionRatio(DT_TIME, expectType);
		int *pbuf = (int*)res->getDataArray();
		if(ratio < 0) ratio = -ratio;
		for(int i = 0; i < size_; i++){
			int remainder = data_[i] % 86400000;
			data_[i] == LLONG_MIN ? pbuf[i] = INT_MIN : pbuf[i] = (remainder + ((data_[i] < 0) && remainder) * 86400000) / ratio;
		}
	}
	return res;
}

ConstantSP FastNanoTimestampVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		return retrieve((Vector*)index.get());
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return ConstantSP(new NanoTimestamp(idx<(UINDEX)size_?data_[idx]:nullVal_));
	}
}

ConstantSP FastNanoTimestampVector::castTemporal(DATA_TYPE expectType){
	if(expectType != DT_DATEHOUR && (expectType < DT_DATE || expectType > DT_NANOTIMESTAMP)){
		throw RuntimeException("castTemporal from NANOTIMESTAMP to "+ Util::getDataTypeString(expectType)+" not supported ");
	}
	if(expectType == DT_NANOTIMESTAMP)
		return getValue();

	VectorSP res = Util::createVector(expectType, size_);
	if(expectType == DT_DATEHOUR){
        int *pbuf = (int*)res->getDataArray();
        for(int i = 0; i < size_; i++){
			int tail = (data_[i] < 0) && (data_[i] % 3600000000000ll);
            data_[i] == LLONG_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] / 3600000000000ll - tail;
        }
        return res;
	}
	long long ratio = -Util::getTemporalConversionRatio(DT_NANOTIMESTAMP, expectType);
	if(expectType == DT_TIMESTAMP){
		long long *pbuf = (long long*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			int tail = (data_[i] < 0) && (data_[i] % ratio);
			data_[i] == LLONG_MIN ? pbuf[i] = LLONG_MIN : pbuf[i] = data_[i] / ratio - tail;
		}
	}
	else if(expectType == DT_DATE || expectType == DT_DATETIME){
		int *pbuf = (int*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			int tail = (data_[i] < 0) && (data_[i] % ratio);
			data_[i] == LLONG_MIN ? pbuf[i] = INT_MIN : pbuf[i] = data_[i] / ratio - tail;
		}
	}
	else if(expectType == DT_MONTH){
		int *pbuf = (int*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			if(data_[i]  == LLONG_MIN){
				pbuf[i] = INT_MIN;
				continue;
			}
			int year, month, day;
			Util::parseDate(data_[i] / 86400000000000ll, year, month, day);
			pbuf[i] = year*12+month-1;
		}
	}
	else if(expectType == DT_NANOTIME){
		long long *pbuf = (long long*)res->getDataArray();
		for(int i = 0; i < size_; i++){
			long long remainder = data_[i] % 86400000000000ll;
			data_[i] == LLONG_MIN ? pbuf[i] = LLONG_MIN : pbuf[i] = (remainder + (data_[i] < 0 && remainder) * 86400000000000ll);
		}
	}
	else{
		ratio = Util::getTemporalConversionRatio(DT_NANOTIME, expectType);
		int *pbuf = (int*)res->getDataArray();
		ratio = -ratio;
		for(int i = 0; i < size_; i++){
			long long remainder = data_[i] % 86400000000000ll;
			data_[i] == LLONG_MIN ? pbuf[i] = INT_MIN : pbuf[i] =(remainder + (data_[i] < 0 && remainder) * 86400000000000ll) / ratio;
		}
	}
	return res;
}

ConstantSP FastBoolMatrix::getValue() const{
	char* data = new char[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(char));
	return ConstantSP(new FastBoolMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastBoolMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getBool();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastBoolMatrix::getRow(INDEX index) const {
	char* data= new char[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastBoolVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastBoolMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	char* data = new char[cols*rows];
	char* dest=data;
	INDEX start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastBoolMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP FastBoolMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

ConstantSP FastCharMatrix::getValue() const {
	char* data = new char[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(char));
	return ConstantSP(new FastCharMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastCharMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getChar();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastCharMatrix::getRow(INDEX index) const {
	char* data= new char[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastCharVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastCharMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	char* data=new char[cols*rows];
	char* dest=data;
	INDEX start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastCharMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP FastCharMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

ConstantSP FastShortMatrix::getValue() const {
	short* data = new short[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(short));
	return ConstantSP(new FastShortMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastShortMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getShort();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastShortMatrix::getRow(INDEX index) const {
	short* data= new short[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastShortVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastShortMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	short* data = new short[cols*rows];
	short* dest=data;
	INDEX start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastShortMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP  FastShortMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

ConstantSP FastIntMatrix::getValue() const{
	int* data = new int[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(int));
	return ConstantSP(new FastIntMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastIntMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getInt();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastIntMatrix::getRow(INDEX index) const {
	int* data = new int[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastIntVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastIntMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	int* data = new int[cols*rows];
	int* dest=data;
	INDEX start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastIntMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP  FastIntMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

ConstantSP FastLongMatrix::getValue() const {
	long long* data = new long long[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(long long));
	return ConstantSP(new FastLongMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastLongMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getLong();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastLongMatrix::getRow(INDEX index) const {
	long long* data = new long long[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastLongVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastLongMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	long long* data = new long long[cols*rows];
	long long* dest=data;
	INDEX start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastLongMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP FastLongMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

ConstantSP FastFloatMatrix::getValue() const {
	float* data = new float[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(float));
	return ConstantSP(new FastFloatMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastFloatMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getFloat();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastFloatMatrix::getRow(INDEX index) const {
	float* data = new float[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastFloatVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastFloatMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	float* data = new float[cols*rows];
	float* dest=data;
	INDEX start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastFloatMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP  FastFloatMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

ConstantSP FastDoubleMatrix::getValue() const {
	double* data = new double[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(double));
	return ConstantSP(new FastDoubleMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastDoubleMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1){
		data_[column*rows_+row]=value->getDouble();
	}
	else{
		fill(column*rows_+row,value->size(),value);
	}
	return true;
}

ConstantSP FastDoubleMatrix::getRow(INDEX index) const {
	double* data = new double[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastDoubleVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastDoubleMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	double* data = new double[cols*rows];
	double* dest=data;
	INDEX start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastDoubleMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP FastDoubleMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

ConstantSP FastDateMatrix::getValue() const {
	int* data = new int[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(int));
	return ConstantSP(new FastDateMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastDateMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getInt();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastDateMatrix::getRow(INDEX index) const {
	int* data = new int[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastDateVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastDateMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	int* data = new int[cols * rows];
	int* dest=data;
	int start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastDateMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP FastDateMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

ConstantSP FastDateTimeMatrix::getValue() const {
	int* data = new int[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(int));
	return ConstantSP(new FastDateTimeMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastDateTimeMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getInt();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastDateTimeMatrix::getRow(INDEX index) const {
	int* data = new int[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastDateTimeVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastDateTimeMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	int* data = new int[cols * rows];
	int* dest=data;
	int start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastDateTimeMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP FastDateTimeMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

ConstantSP FastDateHourMatrix::getValue() const {
    int* data = new int[cols_ * rows_];
    memcpy(data, data_, size_ * sizeof(int));
    return ConstantSP(new FastDateTimeMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastDateHourMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
    if(value->size()==1)
        data_[column*rows_+row]=value->getInt();
    else
        fill(column*rows_+row,value->size(),value);
    return true;
}

ConstantSP FastDateHourMatrix::getRow(INDEX index) const {
    int* data = new int[cols_];
    for(int i=0;i<cols_;++i)
        data[i]=data_[i*rows_+index];
    VectorSP row=ConstantSP(new FastDateTimeVector(cols_,0,data,containNull_));
    if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
    return row;
}

ConstantSP FastDateHourMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
    int cols=std::abs(colLength);
    int rows=std::abs(rowLength);
    int* data = new int[cols * rows];
    int* dest=data;
    int start=rowStart+colStart*rows_;
    bool reverseCol=colLength<0;
    for(int i=0;i<cols;++i){
        getDataArray(start,rowLength,dest);
        if(reverseCol)
            start-=rows_;
        else
            start+=rows_;
        dest+=rows;
    }
    ConstantSP matrix=ConstantSP(new FastDateTimeMatrix(cols,rows,cols,data,containNull_));
    if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
    if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
    return matrix;
}

ConstantSP FastDateHourMatrix::getColumn(INDEX index) const {
    VectorSP col = getSubVector(rows_ * index, rows_);
    if (!colLabel_->isNull()) col->setName(colLabel_->getString(index));
    return col;
}

ConstantSP FastMonthMatrix::getValue() const {
	int* data = new int[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(int));
	return ConstantSP(new FastMonthMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastMonthMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getInt();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastMonthMatrix::getRow(INDEX index) const {
	int* data = new int[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastMonthVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastMonthMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	int* data = new int[cols * rows];
	int* dest=data;
	int start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastMonthMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP FastMonthMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

ConstantSP FastTimeMatrix::getValue() const {
	int* data = new int[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(int));
	return ConstantSP(new FastTimeMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastTimeMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getInt();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastTimeMatrix::getRow(INDEX index) const {
	int* data = new int[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastTimeVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastTimeMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	int* data = new int[cols * rows];
	int* dest=data;
	int start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastTimeMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP FastTimeMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

ConstantSP FastSecondMatrix::getValue() const {
	int* data = new int[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(int));
	return ConstantSP(new FastSecondMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastSecondMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getInt();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastSecondMatrix::getRow(INDEX index) const {
	int* data = new int[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastSecondVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastSecondMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	int* data = new int[cols * rows];
	int* dest=data;
	int start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastSecondMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP FastSecondMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

ConstantSP FastMinuteMatrix::getValue() const {
	int* data = new int[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(int));
	return ConstantSP(new FastMinuteMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastMinuteMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getInt();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastMinuteMatrix::getRow(INDEX index) const {
	int* data = new int[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastMinuteVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastMinuteMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	int* data = new int[cols * rows];
	int* dest=data;
	int start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastMinuteMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP  FastMinuteMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

ConstantSP FastNanoTimeMatrix::getValue() const {
	long long* data = new long long[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(long long));
	return ConstantSP(new FastNanoTimeMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastNanoTimeMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getLong();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastNanoTimeMatrix::getRow(INDEX index) const {
	long long* data = new long long[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastNanoTimeVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastNanoTimeMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	long long* data = new long long[cols * rows];
	long long* dest=data;
	int start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastNanoTimeMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP FastNanoTimeMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}


ConstantSP FastTimestampMatrix::getValue() const {
	long long* data = new long long[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(long long));
	return ConstantSP(new FastTimestampMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastTimestampMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getLong();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastTimestampMatrix::getRow(INDEX index) const {
	long long* data = new long long[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastTimestampVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastTimestampMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	long long* data = new long long[cols * rows];
	long long* dest=data;
	int start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastTimestampMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP FastTimestampMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

ConstantSP FastNanoTimestampMatrix::getValue() const {
	long long* data = new long long[cols_ * rows_];
	memcpy(data, data_, size_ * sizeof(long long));
	return ConstantSP(new FastNanoTimestampMatrix(cols_,rows_,cols_,data,containNull_));
}

bool FastNanoTimestampMatrix::set(INDEX column, INDEX row, const ConstantSP& value){
	if(value->size()==1)
		data_[column*rows_+row]=value->getLong();
	else
		fill(column*rows_+row,value->size(),value);
	return true;
}

ConstantSP FastNanoTimestampMatrix::getRow(INDEX index) const {
	long long* data = new long long[cols_];
	for(int i=0;i<cols_;++i)
		data[i]=data_[i*rows_+index];
	VectorSP row=ConstantSP(new FastNanoTimestampVector(cols_,0,data,containNull_));
	if(!rowLabel_->isNull()) row->setName(rowLabel_->getString(index));
	return row;
}

ConstantSP FastNanoTimestampMatrix::getWindow(INDEX colStart, int colLength,INDEX rowStart, int rowLength) const {
	int cols=std::abs(colLength);
	int rows=std::abs(rowLength);
	long long* data = new long long[cols * rows];
	long long* dest=data;
	int start=rowStart+colStart*rows_;
	bool reverseCol=colLength<0;
	for(int i=0;i<cols;++i){
		getDataArray(start,rowLength,dest);
		if(reverseCol)
			start-=rows_;
		else
			start+=rows_;
		dest+=rows;
	}
	ConstantSP matrix=ConstantSP(new FastNanoTimestampMatrix(cols,rows,cols,data,containNull_));
	if(!rowLabel_->isNull()) matrix->setRowLabel(((Vector*)rowLabel_.get())->getSubVector(rowStart,rowLength));
	if(!colLabel_->isNull()) matrix->setColumnLabel(((Vector*)colLabel_.get())->getSubVector(colStart,colLength));
	return matrix;
}

ConstantSP FastNanoTimestampMatrix::getColumn(INDEX index) const {
	VectorSP col=getSubVector(rows_*index,rows_);
	if(!colLabel_->isNull()) col->setName(colLabel_->getString(index));
	return col;
}

void FastFixedLengthVector::clear(){
	size_ = 0;
	containNull_ = false;
}

void FastFixedLengthVector::initialize(){
	memset((void*)data_,0,fixedLength_*size_);
}

FastFixedLengthVector::FastFixedLengthVector(DATA_TYPE type, int fixedLength, int size, int capacity, unsigned char* srcData, bool containNull): fixedLength_(fixedLength), size_(size),
		capacity_(capacity), type_(type), containNull_(containNull){
	if(capacity<size)
		capacity_=size;
	data_ = srcData;
}

FastFixedLengthVector::~FastFixedLengthVector(){
	delete[] data_;
}

INDEX FastFixedLengthVector::reserve(INDEX capacity){
	if(capacity > capacity_){
		INDEX newCapacity= (std::max)((INDEX)(capacity_ * 1.2), capacity);
		unsigned char* newData = new unsigned char[newCapacity * fixedLength_];
		memcpy(newData,data_,size_*fixedLength_);
		delete[] data_;
		data_=newData;
		capacity_=newCapacity;
	}
	return capacity_;
}

unsigned char* FastFixedLengthVector::getDataArray(const Vector* indexVector, bool& hasNull) const {
	INDEX len = indexVector->size();
	INDEX bytes = len * fixedLength_;
	unsigned char* buf = new unsigned char[bytes];
	UINDEX size=size_;
	unsigned char* dest = buf;
	hasNull = false;
	if(indexVector->isIndexArray()){
		UINDEX* bufIndex=(UINDEX*)indexVector->getIndexArray();
		for(INDEX i=0;i<len;++i){
			if(bufIndex[i]<size)
				memcpy(dest, data_ + bufIndex[i] * fixedLength_, fixedLength_);
			else{
				memset(dest, 0, fixedLength_);
				hasNull=true;
			}
			dest += fixedLength_;
		}
	}
	else{
		UINDEX bufIndex[Util::BUF_SIZE];
		const UINDEX* pbufIndex;
		INDEX start=0;
		int count=0;
		int i;
		while(start<len){
			count=(std::min)(len-start,Util::BUF_SIZE);
			pbufIndex = (const UINDEX*)indexVector->getIndexConst(start,count,(INDEX*)bufIndex);
			for(i=0;i<count;++i){
				if(pbufIndex[i]<size)
					memcpy(dest, data_ + pbufIndex[i] * fixedLength_, fixedLength_);
				else{
					memset(dest, 0, fixedLength_);
					hasNull=true;
				}
				dest += fixedLength_;
			}
			start+=count;
		}
	}
	if(containNull_ && !hasNull){
		hasNull = hasNullInRange(buf, 0, len);
	}
	return buf;
}

unsigned char* FastFixedLengthVector::getDataArray(INDEX start, INDEX length) const {
	return getDataArray(start, length, std::abs(length));
}

unsigned char* FastFixedLengthVector::getDataArray(INDEX start, INDEX length, INDEX capacity) const {
	unsigned char* buf= new unsigned char[capacity*fixedLength_];
	if(length>0)
		memcpy(buf,data_+start*fixedLength_,length*fixedLength_);
	else{
		unsigned char* src = data_ + start * fixedLength_;
		unsigned char* dest = buf;
		length=std::abs(length);
		while(length>0){
			memcpy(dest, src, fixedLength_);
			dest += fixedLength_;
			src -= fixedLength_;
			--length;
		}
	}
	return buf;
}


void FastFixedLengthVector::getDataArray(INDEX start, INDEX length, unsigned char* buf) const {
	if(length>0)
		memcpy(buf, data_+start*fixedLength_, length*fixedLength_);
	else{
		unsigned char* src=data_+start * fixedLength_;
		unsigned char* dest=buf;
		length=std::abs(length);
		while(length>0){
			memcpy(dest, src, fixedLength_);
			dest += fixedLength_;
			src -= fixedLength_;
			--length;
		}
	}
}

bool FastFixedLengthVector::checkCapacity(int appendSize){
	if(size_+appendSize>capacity_){
		INDEX newCapacity= (size_+appendSize)*1.2;
		unsigned char* newData = new unsigned char[newCapacity * fixedLength_];
		memcpy(newData,data_,size_ * fixedLength_);
		delete[] data_;
		capacity_=newCapacity;
		data_=newData;
	}
	return true;
}

ConstantSP FastFixedLengthVector::getSubVector(INDEX start, INDEX length, INDEX capacity) const {
	if(capacity <= 0)
		capacity = 1;
	DATA_TYPE type = getType();
	unsigned char* data = getDataArray(start,length,capacity);
	if(data)
		return Util::createVector(type,std::abs(length), capacity, true, getExtraParamForType(), data, containNull_);
	else
		throw MemoryException();
}

IO_ERR FastFixedLengthVector::deserialize(DataInputStream* in, INDEX indexStart, INDEX targetNumElement, INDEX& numElement){
	IO_ERR ret;
	INDEX end = indexStart + targetNumElement;
	if(end > capacity_ && !checkCapacity(end - size_))
		return NOSPACE;
	INDEX i=indexStart;
	size_t unitLength = fixedLength_;
	if(!in->isIntegerReversed()){
		size_t actualLength;
		ret = in->readBytes((char*)(data_ + i*fixedLength_), unitLength,  targetNumElement, actualLength);
		i += actualLength;
	}
	else{
		for(; i<end; ++i){
			if((ret = in->readBytes((char*)(data_+i*fixedLength_), unitLength, true)) != OK){
				numElement = i - indexStart;
				if(i > size_)
					size_ = i;
				if(!containNull_)
					containNull_ = hasNullInRange(data_, indexStart, i);
				return ret;
			}
		}
	}
	numElement = i - indexStart;
	if(i > size_)
		size_ = i;
	if(!containNull_)
		containNull_ = hasNullInRange(data_, indexStart, i);
	return ret;
}

ConstantSP FastFixedLengthVector::retrieve(Vector* index) const {
	INDEX length = index->size();
	bool hasNull =containNull_;
	DATA_TYPE type = getType();
	unsigned char* data = getDataArray(index, hasNull);
	return Util::createVector(type,length, 0, true, getExtraParamForType(), (void*)data, hasNull);
}

ConstantSP FastFixedLengthVector::getInstance(INDEX size) const {
	DATA_TYPE type = getType();
	INDEX capacity = (std::max)(1, size);
	INDEX bytes = capacity * fixedLength_;
	unsigned char* data = new unsigned char[bytes];
	return Util::createVector(type,size, capacity, true, getExtraParamForType(), (void*)data, false);
}

ConstantSP FastFixedLengthVector::getValue() const {
	DATA_TYPE type = getType();
		unsigned char* data = getDataArray(0, size_);
	Vector* copy = Util::createVector(type,size_, 0, true, getExtraParamForType(), (void*)data, containNull_);
	copy->setForm(getForm());
	return copy;
}

ConstantSP FastFixedLengthVector::getValue(INDEX capacity) const {
	DATA_TYPE type = getType();
	capacity = (std::max)(capacity, (INDEX)size_);
	unsigned char*	data = new unsigned char[capacity * fixedLength_];
	memcpy(data, data_, fixedLength_ * size_);
	Vector* copy = Util::createVector(type,size_, capacity, true, getExtraParamForType(), (void*)data, containNull_);
	copy->setForm(getForm());
	return copy;
}

bool FastFixedLengthVector::set(INDEX index, const ConstantSP& value){
	value->getBinary(0, 1, fixedLength_, data_ + index * fixedLength_);
	if(!containNull_ && value->isNull())
		containNull_=true;
	return true;
}

bool FastFixedLengthVector::set(const ConstantSP& index, const ConstantSP& value){
	if(value->getType() != type_)
		return false;

	if(index->isVector()){
		INDEX len=index->size();
		INDEX bufSize = (std::min)(len, Util::BUF_SIZE);
		//INDEX bufIndex[bufSize];
		std::unique_ptr<INDEX[]> bufIndex(new INDEX[bufSize]);
		//unsigned char bufVal[bufSize * fixedLength_];
		std::unique_ptr<unsigned char[]> bufVal(new unsigned char[bufSize * fixedLength_]);
		const INDEX* pindex;
		const unsigned char* pval;
		INDEX start=0;
		int count;
		while(start<len){
			count=(std::min)(len-start,Util::BUF_SIZE);
			pindex=index->getIndexConst(start,count,bufIndex.get());
			pval=value->getBinaryConst(start, count, fixedLength_, bufVal.get());
			for(int i=0;i<count;i++)
				memcpy(data_ + pindex[i]*fixedLength_, pval + i * fixedLength_, fixedLength_);
			start+=count;
		}
		if(!containNull_ && value->getNullFlag())
			containNull_ = true;
	}
	else{
		value->getBinary(0, 1, fixedLength_, data_ + index->getIndex() * fixedLength_);
		if(!containNull_ && value->isNull())
			containNull_ = true;
	}
	return true;
}

void FastFixedLengthVector::fill(INDEX start, INDEX length, const ConstantSP& value){
	if(!value->getBinary(0, length, fixedLength_, data_ + start * fixedLength_))
		throw RuntimeException("Failed to read binary data from the given vector.");
	else if(!containNull_){
		if(value->getNullFlag())
			containNull_ = true;
	}
}

bool FastFixedLengthVector::append(const ConstantSP& value, INDEX appendSize){
	if(!checkCapacity(appendSize))
		return false;

	if(!value->getBinary(0,appendSize,fixedLength_,data_ + size_ * fixedLength_))
		return false;
	else if(!containNull_){
		if(value->getNullFlag())
			containNull_ = true;
	}
	size_+=appendSize;
	return true;
}

bool FastFixedLengthVector::remove(const ConstantSP& index){
	INDEX size = index->size();
	INDEX invSize = size_ - size;
	if(invSize <= 0){
		size_ = 0;
		containNull_ = false;
		return true;
	}

	INDEX* a[1];
	INDEX** dataSeg = a;
	INDEX segmentSize  = size;
	int segCount = 1;
	if(index->isIndexArray())
		dataSeg[0] = index->getIndexArray();
	else if(index->isHugeIndexArray()){
		dataSeg = index->getHugeIndexArray();
		segmentSize = index->getSegmentSize();
		segCount = (size >> index->getSegmentSizeInBit()) + (size & (segmentSize - 1) ? 1 : 0);
	}
	else
		return false;
	INDEX prevIndex = dataSeg[0][0];
	INDEX cursor = prevIndex;
	INDEX j = 1;

	for(int i=0; i<segCount; ++i){
		INDEX* delIndices = dataSeg[i];
		INDEX count = (std::min)(segmentSize, size - i * segmentSize);
		for(; j<count; ++j){
			if(delIndices[j] > prevIndex + 1){
				INDEX end = delIndices[j];
				int len = end - (prevIndex + 1);
				memmove(data_ + cursor * fixedLength_, data_+(prevIndex+1)*fixedLength_, len*fixedLength_);
				cursor += len;
			}
			prevIndex = delIndices[j];
		}
		j = 0;
	}
	int len = size_ - (prevIndex + 1);
	memmove(data_ + cursor * fixedLength_, data_+(prevIndex+1)*fixedLength_, len*fixedLength_);

	size_ = invSize;
	if(containNull_){
		containNull_ = hasNullInRange(data_, 0, size_);
	}
	return true;
}

bool FastFixedLengthVector::remove(INDEX count){
	if(!sizeable() || std::abs(count)>size_)
		return false;
	if(count<0){
		count=-count;
		memmove(data_, data_+count*fixedLength_, (size_-count)*fixedLength_);
	}
	size_-=count;
	return true;
}

void FastFixedLengthVector::next(INDEX steps){
   	if(steps>size_ || steps<0)
    	return;
	memmove(data_, data_+steps*fixedLength_, (size_-steps)*fixedLength_);
	memset(data_ + (size_ - steps)*fixedLength_, 0, steps * fixedLength_);
	containNull_=true;
}

void FastFixedLengthVector::prev(INDEX steps){
	if(steps>size_ || steps<0)
		return;
	memmove(data_+ steps*fixedLength_, data_, (size_-steps)*fixedLength_);
	memset(data_, 0, steps * fixedLength_);
	containNull_=true;
}

long long FastFixedLengthVector::getAllocatedMemory() const {
	return fixedLength_* (std::max)(capacity_, size_);
}

int FastFixedLengthVector::serialize(char* buf, int bufSize, INDEX indexStart, int offset, int& numElement, int& partial) const {
	//assume offset==0 and bufSize>=sizeof(T)
	if(indexStart >= size_)
		return -1;
	int len = fixedLength_;
	partial = 0;
	numElement = (std::min)(bufSize / len, size_ -indexStart);
	memcpy(buf, data_+indexStart * fixedLength_, len * numElement);
	return len * numElement;
}

bool FastFixedLengthVector::assign(const ConstantSP& value){
	if(value->isVector()){
		if(size_!=value->size())
			return false;
	}
	fill(0,size_,value);
	return true;
}

void FastFixedLengthVector::setNull(INDEX index){
	memset(data_ + index * fixedLength_, 0, fixedLength_);
}

void FastFixedLengthVector::reverse(INDEX start, INDEX length){
	INDEX len=length/2;
	INDEX end = start + length - 1;
	//unsigned char tmp[fixedLength_];
	std::unique_ptr<unsigned char[]> tmp(new unsigned char[fixedLength_]);
	unsigned char* data = data_ + start * fixedLength_;
	unsigned char* src = data_ + end * fixedLength_;
	for(INDEX i=0; i<len; ++i){
		memcpy(tmp.get(), data, fixedLength_);
		memcpy(data, src, fixedLength_);
		memcpy(src, tmp.get(), fixedLength_);
		data += fixedLength_;
		src -= fixedLength_;
	}
}

bool FastFixedLengthVector::getBinary(INDEX start, int len, int unitLenght, unsigned char* buf) const {
	memcpy(buf, data_ + start * fixedLength_, len * fixedLength_);
	return true;
}

const unsigned char* FastFixedLengthVector::getBinaryConst(INDEX start, int len, int unitLength, unsigned char* buf) const {
	return data_ + start * fixedLength_;
}

unsigned char* FastFixedLengthVector::getBinaryBuffer(INDEX start, int len, int unitLength, unsigned char* buf) const {
	return data_ + start * fixedLength_;
}

void* FastFixedLengthVector::getDataBuffer(INDEX start, int len, void* buf) const {
	return (void*)(data_ + start * fixedLength_);
}

void FastFixedLengthVector::setBinary(INDEX index, int unitLength, const unsigned char* val){
	memcpy(data_ + index * fixedLength_, val, fixedLength_);
}

bool FastFixedLengthVector::setBinary(INDEX start, int len, int unitLength, const unsigned char* buf){
	if(buf == data_ + start*fixedLength_)
		return true;
	memcpy(data_ + start * fixedLength_, buf, fixedLength_ * len);
	return true;
}

bool FastFixedLengthVector::setData(INDEX start, int len, void* buf){
	if(buf != (void*)(data_+start*fixedLength_))
		memcpy(data_+start*fixedLength_, buf, fixedLength_*len);
	return true;
}

FastInt128Vector::FastInt128Vector(DATA_TYPE type, int size, int capacity, unsigned char* srcData, bool containNull) : FastRecordVector<Guid, GuidHash>(type, size, capacity, srcData, containNull){

}

const Guid FastInt128Vector::getInt128() const {
	if(size_ == 1)
		return Guid(data_);
	else
		throw RuntimeException("A scalar object is expected. But the actual object is a vector.");
}

FastUuidVector::FastUuidVector(int size, int capacity, unsigned char* srcData, bool containNull) : FastInt128Vector(DT_UUID, size, capacity, srcData, containNull){
}

FastIPAddrVector::FastIPAddrVector(int size, int capacity, unsigned char* srcData, bool containNull) : FastInt128Vector(DT_IP, size, capacity, srcData, containNull){
}

bool FastSymbolVector::set(INDEX index, const ConstantSP& value){
	data_[index] = base_->findAndInsert(value->getString());
	if(data_[index]==nullVal_)
		containNull_=true;
	return true;
}

ConstantSP  FastSymbolVector::get(INDEX index) const {
	return ConstantSP(new String(getString(index)));
}

ConstantSP  FastSymbolVector::get(const ConstantSP& index) const {
	if(index->isVector()){
		INDEX len=index->size();
		StringVector* p=new StringVector(len, len, false);
		string *pdata=(string*)p->getDataArray();
		ConstantSP result(p);
		if(index->isIndexArray()){
			UINDEX* bufIndex=(UINDEX*)index->getIndexArray();
			for(INDEX i=0;i<len;++i)
				pdata[i] = (int)bufIndex[i] < size_ ? base_->getSymbol(data_[bufIndex[i]]) : "";
		}
		else{
			const int bufSize=Util::BUF_SIZE;
			UINDEX bufIndex[bufSize];
			INDEX start=0;
			int count=0;
			int i;
			while(start<len){
				count=((std::min))(len-start,bufSize);
				index->getIndex(start,count,(INDEX*)bufIndex);
				for(i=0;i<count;i++){
					pdata[start+i]=(int)bufIndex[i] < size_ ? base_->getSymbol(data_[bufIndex[i]]) : "";
				}
				start+=count;
			}
		}
		p->setNullFlag(containNull_ || p->hasNull());
		return result;
	}
	else{
		UINDEX idx=(UINDEX)index->getIndex();
		return get(idx);
	}
}

void  FastSymbolVector::fill(INDEX start, INDEX length, const ConstantSP& value){
	if(value->size()==1 || value->size()!=length){
		int fillVal = base_->findAndInsert(value->getString());
		int end=start+length;
		for(int i=start;i<end;++i)
			data_[i]=fillVal;
	}
	else{
		if(value->getCategory() != LITERAL || value->size() < length)
			throw RuntimeException("Failed to read int data from the given vector.");
		for(int i = 0; i < length; i++){
			int fillVal = base_->findAndInsert(value->getString(i));
			data_[i + start]=fillVal;
		}
	}
	if(value->getNullFlag())
		containNull_=true;
}

bool  FastSymbolVector::validIndex(INDEX uplimit){
	return validIndex(0, size_, uplimit);
}

bool  FastSymbolVector::validIndex(INDEX start, INDEX length, INDEX uplimit){
	unsigned int limit=(unsigned int)uplimit;
	unsigned int* data=(unsigned int*)data_;
	INDEX end = start + length;
	for(INDEX i=start;i<end;++i)
		if(data[i]>limit)
			return false;
	return true;
}

bool FastSymbolVector::set(const ConstantSP& index, const ConstantSP& value){
	if(index->isVector()){
		INDEX len=index->size();
		INDEX bufIndex[Util::BUF_SIZE];
		const INDEX* pindex;
		INDEX start=0;
		int count;
		while(start<len){
			count=((std::min))(len-start,Util::BUF_SIZE);
			pindex=index->getIndexConst(start,count,bufIndex);
			for(int i=0;i<count;i++)
				data_[pindex[i]]=base_->findAndInsert(value->getString(start + i));
			start+=count;
		}
	}
	else
		data_[index->getIndex()]=base_->findAndInsert(value->getString());
	if(value->getNullFlag())
		containNull_=true;
	return true;
}
};
