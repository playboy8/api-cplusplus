/*
 * DolphinDB.cpp
 *
 *  Created on: Sep 22, 2018
 *      Author: dzhou
 */

#include <ctime>
#include <fstream>
#include <istream>
#include <stack>
#ifndef WINDOWS
#include <uuid/uuid.h>
#else
#include <Objbase.h>
#endif

#include "Concurrent.h"
#include "ConstantImp.h"
#include "ConstantMarshall.h"
#include "DolphinDB.h"
#include "ScalarImp.h"
#include "Util.h"

using std::ifstream;

#ifdef INDEX64
namespace std {
int min(int a, INDEX b) {
    return a < b ? a : (int)b;
}

int min(INDEX a, int b) {
    return a < b ? (int)a : b;
}
}    // namespace std
#endif

static inline uint32_t murmur32_16b (const unsigned char* key){
	const uint32_t m = 0x5bd1e995;
	const int r = 24;
	uint32_t h = 16;

	uint32_t k1 = *(uint32_t*)(key);
	uint32_t k2 = *(uint32_t*)(key + 4);
	uint32_t k3 = *(uint32_t*)(key + 8);
	uint32_t k4 = *(uint32_t*)(key + 12);

	k1 *= m;
	k1 ^= k1 >> r;
	k1 *= m;

	k2 *= m;
	k2 ^= k2 >> r;
	k2 *= m;

	k3 *= m;
	k3 ^= k3 >> r;
	k3 *= m;

	k4 *= m;
	k4 ^= k4 >> r;
	k4 *= m;

	// Mix 4 bytes at a time into the hash
	h *= m;
	h ^= k1;
	h *= m;
	h ^= k2;
	h *= m;
	h ^= k3;
	h *= m;
	h ^= k4;

	// Do a few final mixes of the hash to ensure the last few
	// bytes are well-incorporated.
	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}

namespace dolphindb {

class EXPORT_DECL DBConnectionImpl {
public:
    DBConnectionImpl(bool sslEnable = false, bool asynTask = false);
    ~DBConnectionImpl();
    bool connect(const string& hostName, int port, const string& userId = "", const string& password = "",bool sslEnable = false, bool asynTask = false);
    void login(const string& userId, const string& password, bool enableEncryption);
    ConstantSP run(const string& script, int priority = 4, int parallelism = 2, int fetchSize = 0);
    ConstantSP run(const string& funcName, vector<ConstantSP>& args, int priority = 4, int parallelism = 2, int fetchSize = 0);
    void upload(const string& name, const ConstantSP& obj);
    void upload(vector<string>& names, vector<ConstantSP>& objs);
    void close();
    static void initialize();

private:
    ConstantSP run(const string& script, const string& scriptType, vector<ConstantSP>& args, int priority = 4, int parallelism = 2,int fetchSize = 0);
    bool connect();
    void login();

private:
    SocketSP conn_;
    string sessionId_;
    string hostName_;
    int port_;
    string userId_;
    string pwd_;
    bool encrypted_;
    bool isConnected_;
    bool littleEndian_;
    bool sslEnable_;
    bool asynTask_;
    static bool initialized_;
};

bool DBConnectionImpl::initialized_ = false;
string Constant::EMPTY("");
string Constant::NULL_STR("NULL");
ConstantSP Constant::void_(new Void());
ConstantSP Constant::null_(new Void(true));
ConstantSP Constant::true_(new Bool(true));
ConstantSP Constant::false_(new Bool(false));
ConstantSP Constant::one_(new Int(1));

Guid::Guid(bool newGuid) {
    if (!newGuid) {
        memset(uuid_, 0, 16);
    } else {
#ifdef WINDOWS
        CoCreateGuid((GUID*)uuid_);
#else
        uuid_generate(uuid_);
#endif
    }
}

Guid::Guid(unsigned char* guid) {
    memcpy(uuid_, guid, 16);
}

Guid::Guid(const string& guid) {
	if(guid.size() != 36 || !Util::fromGuid(guid.c_str(), uuid_))
		throw RuntimeException("Invalid UUID string");
}

Guid::Guid(const Guid& copy) {
    memcpy(uuid_, copy.uuid_, 16);
}

bool Guid::isZero() const {
    const unsigned char* a = (const unsigned char*)uuid_;
    return (*(long long*)a) == 0 && (*(long long*)(a + 8)) == 0;
}

string Guid::getString() const {
	return getString(uuid_);
}

string Guid::getString(const unsigned char* uuid) {
	char buf[36];
	Util::toGuid(uuid, buf);
	return string(buf, 36);
}

uint64_t GuidHash::operator()(const Guid& guid) const {
   return murmur32_16b(guid.bytes());
}

ConstantSP Constant::getRowLabel() const {
    return void_;
}

ConstantSP Constant::getColumnLabel() const {
    return void_;
}

ConstantSP Vector::getColumnLabel() const {
    return new String(name_);
}

string Vector::getString() const {
    if (getForm() == DF_PAIR) {
        return getScript();
    } else {
        int len = (std::min)(Util::DISPLAY_ROWS, size());
        bool notTuple = getType() != DT_ANY;
        string str(notTuple ? "[" : "(");

        if (len > 0) {
            if (len == 1 && isNull(0))
                str.append(get(0)->getScript());
            else {
                if (isNull(0)) {
                    // do nothing
                } else if (notTuple || get(0)->isScalar())
                    str.append(get(0)->getScript());
                else
                    str.append(getString(0));
            }
        }
        for (int i = 1; i < len; ++i) {
            str.append(",");
            if (isNull(i)) {
                // do nothing
            } else if (notTuple || get(i)->isScalar())
                str.append(get(i)->getScript());
            else
                str.append(getString(i));
        }
        if (size() > len)
            str.append("...");
        str.append(notTuple ? "]" : ")");
        return str;
    }
}

string Vector::getScript() const {
    if (getForm() == DF_PAIR) {
        string str = get(0)->getScript();
        str.append(" : ");
        str.append(get(1)->getScript());
        return str;
    } else if (getForm() == DF_MATRIX) {
        return name_.empty() ? "matrix()" : name_;
    } else {
        int len = size();
        if (len > Util::CONST_VECTOR_MAX_SIZE)
            return name_.empty() ? "array()" : name_;

        string str("[");
        if (len > 0)
            str.append(get(0)->getScript());
        for (int i = 1; i < len; ++i) {
            str.append(",");
            str.append(get(i)->getScript());
        }
        str.append("]");
        return str;
    }
}

Matrix::Matrix(int cols, int rows) : cols_(cols), rows_(rows), rowLabel_(Constant::void_), colLabel_(Constant::void_) {
}

void Matrix::setRowLabel(const ConstantSP& label) {
    if (label->getType() == DT_VOID)
        rowLabel_ = label;
    else if (label->isTemporary())
        rowLabel_ = label;
    else
        rowLabel_ = label->getValue();
    rowLabel_->setTemporary(false);
}

void Matrix::setColumnLabel(const ConstantSP& label) {
    if (label->getType() == DT_VOID)
        colLabel_ = label;
    else if (label->isTemporary())
        colLabel_ = label;
    else
        colLabel_ = label->getValue();
    colLabel_->setTemporary(false);
}

bool Matrix::reshape(INDEX cols, INDEX rows) {
    if (cols_ == cols && rows_ == rows)
        return true;
    if (cols_ * rows_ != cols * rows && rows != rows_)
        return false;
    cols_ = cols;
    rows_ = rows;
    if (!colLabel_->isNothing() && colLabel_->size() != cols_)
        colLabel_ = Constant::void_;
    if (!rowLabel_->isNothing() && rowLabel_->size() != rows_)
        rowLabel_ = Constant::void_;
    return true;
}
string Matrix::getString() const {
    int rows = (std::min)(Util::DISPLAY_ROWS, rows_);
    int limitColMaxWidth = 25;
    int length = 0;
    int curCol = 0;
    int maxColWidth;
    vector<string> list(rows + 1);
    vector<string> listTmp(rows + 1);
    string separator;
    int i, curSize;

    // display row label
    if (!rowLabel_->isNull()) {
        listTmp[0] = "";
        maxColWidth = 0;
        for (i = 0; i < rows; i++) {
            listTmp[i + 1] = rowLabel_->getString(i);
            if ((int)listTmp[i + 1].size() > maxColWidth)
                maxColWidth = listTmp[i + 1].size();
        }

        for (i = 0; i <= rows; i++) {
            curSize = listTmp[i].size();
            if (curSize <= maxColWidth) {
                list[i].append(listTmp[i]);
                if (curSize < maxColWidth)
                    list[i].append(maxColWidth - curSize, ' ');
            } else {
                if (maxColWidth > 3)
                    list[i].append(listTmp[i].substr(0, maxColWidth - 3));
                list[i].append("...");
            }
            list[i].append(1, i == 0 ? ' ' : '|');
        }

        maxColWidth++;
        separator.append(maxColWidth, ' ');
        length += maxColWidth;
    }

    while (length < Util::DISPLAY_WIDTH && curCol < cols_) {
        listTmp[0] = colLabel_->isNull() ? "#" + Util::convert(curCol) : colLabel_->getString(curCol);
        maxColWidth = 0;
        for (i = 0; i < rows; i++) {
            listTmp[i + 1] = getString(curCol, i);
            if ((int)listTmp[i + 1].size() > maxColWidth)
                maxColWidth = listTmp[i + 1].size();
        }
        if (maxColWidth > limitColMaxWidth)
            maxColWidth = limitColMaxWidth;
        if ((int)listTmp[0].size() > maxColWidth)
            maxColWidth = (std::min)(limitColMaxWidth, (int)listTmp[0].size());
        separator.append(maxColWidth, '-');
        if (curCol < cols_ - 1) {
            maxColWidth++;
            separator.append(1, ' ');
        }

        if (length + maxColWidth > Util::DISPLAY_WIDTH && curCol + 1 < cols_)
            break;

        for (i = 0; i <= rows; i++) {
            curSize = listTmp[i].size();
            if (curSize <= maxColWidth) {
                list[i].append(listTmp[i]);
                if (curSize < maxColWidth)
                    list[i].append(maxColWidth - curSize, ' ');
            } else {
                if (maxColWidth > 3)
                    list[i].append(listTmp[i].substr(0, maxColWidth - 3));
                list[i].append("...");
            }
        }
        length += maxColWidth;
        curCol++;
    }

    if (curCol < cols_) {
        for (i = 0; i <= rows; i++)
            list[i].append("...");
        separator.append(3, '-');
    }

    string resultStr(list[0]);
    resultStr.append("\n");
    resultStr.append(separator);
    resultStr.append("\n");
    for (i = 1; i <= rows; i++) {
        resultStr.append(list[i]);
        resultStr.append("\n");
    }
    if (rows < rows_)
        resultStr.append("...\n");
    return resultStr;
}

string Matrix::getString(INDEX index) const {
    int len = (std::min)(Util::DISPLAY_ROWS, rows_);
    string str("{");

    if (len > 0)
        str.append(getString(index, 0));
    for (int i = 1; i < len; ++i) {
        str.append(",");
        str.append(getString(index, i));
    }
    if (rows_ > len)
        str.append("...");
    str.append("}");
    return str;
}

ConstantSP Matrix::get(const ConstantSP& index) const {
    if (index->isScalar()) {
        int col = index->getInt();
        if (col < 0 || col >= cols_)
            throw OperatorRuntimeException("matrix", "The column index " + Util::convert(col) + " is out of range.");
        return getColumn(col);
    }

    ConstantSP indexCols(index);
    if (index->isPair()) {
        int colStart = index->isNull(0) ? 0 : index->getInt(0);
        int colEnd = index->isNull(1) ? cols_ : index->getInt(1);
        int length = std::abs(colEnd - colStart);

        indexCols = Util::createIndexVector(length, true);
        INDEX* data = indexCols->getIndexArray();
        if (colStart <= colEnd) {
            for (int i = 0; i < length; ++i)
                data[i] = colStart + i;
        } else {
            --colStart;
            for (int i = 0; i < length; ++i)
                data[i] = colStart - i;
        }
    }

    // create a matrix
    int cols = indexCols->size();
    ConstantSP result = getInstance(cols);
    if (!rowLabel_.isNull())
        result->setRowLabel(rowLabel_->getValue());
    if (!colLabel_.isNull()) {
        result->setColumnLabel(colLabel_->get(indexCols));
    }
    for (int i = 0; i < cols; ++i) {
        int cur = indexCols->getInt(i);
        if (cur < 0 || cur >= cols_)
            throw OperatorRuntimeException("matrix", "The column index " + Util::convert(cur) + " is out of range.");
        result->setColumn(i, getColumn(cur));
    }
    return result;
}

bool Matrix::set(const ConstantSP index, const ConstantSP& value) {
    int cols = index->size();
    bool scalar = value->isScalar();
    if (value->size() != rows_ * cols && !scalar)
        throw OperatorRuntimeException("matrix", "matrix and assigned value are not compatible");
    if (cols == 1) {
        int cur = index->getInt(0);
        if (cur >= cols_ || cur < 0)
            throw OperatorRuntimeException("matrix", "The column index " + Util::convert(cur) + " is out of range.");
        setColumn(cur, value);
        return true;
    }

    for (int i = 0; i < cols; ++i) {
        int cur = index->getInt(i);
        if (cur >= cols_ || cur < 0)
            throw OperatorRuntimeException("matrix", "The column index " + Util::convert(cur) + " is out of range.");
        setColumn(cur, scalar ? value : ((Vector*)value.get())->getSubVector(rows_ * i, rows_));
    }
    return true;
}

DFSChunkMeta::DFSChunkMeta(const string& path, const Guid& id, int version, int size, CHUNK_TYPE chunkType, const vector<string>& sites, long long cid)
    : Constant(2051), type_(chunkType), replicaCount_(sites.size()), version_(version), size_(size), sites_(0), path_(path), cid_(cid), id_(id) {
    if (replicaCount_ == 0)
        return;
    sites_ = new string[replicaCount_];
    for (int i = 0; i < replicaCount_; ++i)
        sites_[i] = sites[i];
}

DFSChunkMeta::DFSChunkMeta(const string& path, const Guid& id, int version, int size, CHUNK_TYPE chunkType, const string* sites, int siteCount, long long cid)
    : Constant(2051), type_(chunkType), replicaCount_(siteCount), version_(version), size_(size), sites_(0), path_(path), cid_(cid), id_(id) {
    if (replicaCount_ == 0)
        return;
    sites_ = new string[replicaCount_];
    for (int i = 0; i < replicaCount_; ++i)
        sites_[i] = sites[i];
}

DFSChunkMeta::DFSChunkMeta(const DataInputStreamSP& in) : Constant(2051), sites_(0), id_(false) {
    IO_ERR ret = in->readString(path_);
    if (ret != OK)
        throw RuntimeException("Failed to deserialize DFSChunkMeta object.");

    char guid[16];
    in->read(guid, 16);
    in->readInt(version_);
    in->readIndex(size_);
    in->readChar(type_);
    ret = in->readChar(replicaCount_);
    if (ret != OK)
        throw RuntimeException("Failed to deserialize DFSChunkMeta object.");
    if (replicaCount_ > 0)
        sites_ = new string[replicaCount_];
    for (int i = 0; i < replicaCount_; ++i) {
        string site;
        if ((ret = in->readString(site)) != OK)
            throw RuntimeException("Failed to deserialize DFSChunkMeta object.");
        sites_[i] = site;
    }
    id_ = Guid((unsigned char*)guid);
    if (in->readLong(cid_) != OK)
        throw RuntimeException("Failed to deserialize DFSChunkMeta object.");
}

DFSChunkMeta::~DFSChunkMeta() {
    if (sites_)
        delete[] sites_;
}

string DFSChunkMeta::getString() const {
    string str(isTablet() ? "Tablet[" : "FileBlock[");
    str.append(path_);
    str.append(", ");
    str.append(id_.getString());
    str.append(", {");
    for (int i = 0; i < replicaCount_; ++i) {
        if (i > 0)
            str.append(", ");
        str.append(sites_[i]);
    }
    str.append("}, v");
    str.append(std::to_string(version_));
    str.append(", ");
    str.append(std::to_string(size_));
    str.append(", c");
    str.append(std::to_string(cid_));
    if (isSplittable())
        str.append(", splittable]");
    else
        str.append("]");
    return str;
}

long long DFSChunkMeta::getAllocatedMemory() const {
    long long length = 33 + sizeof(sites_) + (1 + replicaCount_) * (1 + sizeof(string)) + path_.size();
    for (int i = 0; i < replicaCount_; ++i)
        length += sites_[i].size();
    return length;
}

ConstantSP DFSChunkMeta::getMember(const ConstantSP& key) const {
    if (key->getCategory() != LITERAL || (!key->isScalar() && !key->isArray()))
        throw RuntimeException("DFSChunkMeta attribute must be string type scalar or vector.");
    if (key->isScalar())
        return getAttribute(key->getString());
    else {
        int keySize = key->size();
        ConstantSP result = Util::createVector(DT_ANY, keySize);
        for (int i = 0; i < keySize; ++i) {
            result->set(i, getAttribute(key->getString(i)));
        }
        return result;
    }
}

ConstantSP DFSChunkMeta::getSiteVector() const {
    ConstantSP vec = new StringVector(replicaCount_, replicaCount_);
    for (int i = 0; i < replicaCount_; ++i)
        vec->setString(i, sites_[i]);
    return vec;
}

ConstantSP DFSChunkMeta::getAttribute(const string& attr) const {
    if (attr == "path")
        return new String(path_);
    else if (attr == "id")
        return new String(id_.getString());
    else if (attr == "cid")
        return new Long(cid_);
    else if (attr == "version")
        return new Int(version_);
    else if (attr == "sites")
        return getSiteVector();
    else if (attr == "size") {
        ConstantSP obj = Util::createConstant(DT_INDEX);
        obj->setIndex(size_);
        return obj;
    } else if (attr == "isTablet")
        return new Bool(isTablet());
    else if (attr == "splittable")
        return new Bool(isSplittable());
    else
        return Constant::void_;
}

ConstantSP DFSChunkMeta::keys() const {
    vector<string> attrs({"path", "id", "version", "size", "isTablet", "splittable", "sites", "cid"});
    return new StringVector(attrs, attrs.size(), false);
}

ConstantSP DFSChunkMeta::values() const {
    ConstantSP result = Util::createVector(DT_ANY, 8);
    result->set(0, new String(path_));
    result->set(1, new String(id_.getString()));
    result->set(2, new Int(version_));
    ConstantSP sizeObj = Util::createConstant(DT_INDEX);
    sizeObj->setIndex(size_);
    result->set(3, sizeObj);
    result->set(4, new Bool(isTablet()));
    result->set(5, new Bool(isSplittable()));
    result->set(6, getSiteVector());
    result->set(7, new Long(cid_));
    return result;
}

void DBConnectionImpl::initialize() {
    if (initialized_)
        return;
    initialized_ = true;
#ifdef WINDOWS
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 1), &wsaData) != 0) {
        throw IOException("Failed to initialize the windows socket.");
    }
#endif
    initFormatters();
}

DBConnectionImpl::DBConnectionImpl(bool sslEnable, bool asynTask): port_(0), encrypted_(false), isConnected_(false), littleEndian_(Util::isLittleEndian()), sslEnable_(sslEnable),asynTask_(asynTask) {
    if (!initialized_)
        initialize();
}

DBConnectionImpl::~DBConnectionImpl() {
    if (!conn_.isNull()) {
        conn_->close();
    }
}

void DBConnectionImpl::close() {
    if (!conn_.isNull()) {
        conn_->close();
        conn_.clear();
    }
    isConnected_ = false;
}

bool DBConnectionImpl::connect(const string& hostName, int port, const string& userId, const string& password, bool sslEnable,bool asynTask) {
    hostName_ = hostName;
    port_ = port;
    userId_ = userId;
    pwd_ = password;
    encrypted_ = false;
    sslEnable_ = sslEnable;
    asynTask_ = asynTask;
    return connect();
}

bool DBConnectionImpl::connect() {
    if (!conn_.isNull()) {
        conn_->close();
        conn_.clear();
    }
    isConnected_ = false;

    SocketSP conn = new Socket(hostName_, port_, true, sslEnable_);
    IO_ERR ret = conn->connect();
    if (ret != OK) {
        return false;
    }

    string body = "connect\n";
    if (!userId_.empty() && !encrypted_)
        body.append("login\n" + userId_ + "\n" + pwd_ + "\nfalse");
    string out("API 0 ");
    out.append(Util::convert((int)body.size()));
    if(asynTask_)
        out.append(" / 4_1_" + std::to_string(4) + "_" + std::to_string(2));
    out.append(1, '\n');
    out.append(body);
    size_t actualLength;
    ret = conn->write(out.c_str(), out.size(), actualLength);
    if (ret != OK)
        throw IOException("Couldn't send login message to the given host/port with IO error type " + std::to_string(ret));

    DataInputStreamSP in = new DataInputStream(conn);
    string line;
    ret = in->readLine(line);
    if (ret != OK)
        throw IOException("Failed to read message from the socket with IO error type " + std::to_string(ret));

    vector<string> headers;
    Util::split(line.c_str(), ' ', headers);
    if (headers.size() != 3)
        throw IOException("Received invalid header");
    string sessionId = headers[0];
    int numObject = atoi(headers[1].c_str());
    bool remoteLittleEndian = (headers[2] != "0");

    if ((ret = in->readLine(line)) != OK)
        throw IOException("Failed to read response message from the socket with IO error type " + std::to_string(ret));
    if (line != "OK")
        throw IOException(line);

    if (numObject == 1) {
        short flag;
        if ((ret = in->readShort(flag)) != OK)
            throw IOException("Failed to read object flag from the socket with IO error type " + std::to_string(ret));
        DATA_FORM form = static_cast<DATA_FORM>(flag >> 8);

        ConstantUnmarshallFactory factory(in);
        ConstantUnmarshall* unmarshall = factory.getConstantUnmarshall(form);
        if (!unmarshall->start(flag, true, ret)) {
            unmarshall->reset();
            throw IOException("Failed to parse the incoming object with IO error type " + std::to_string(ret));
        }
        ConstantSP result = unmarshall->getConstant();
        unmarshall->reset();
        if (!result->getBool())
            throw IOException("Failed to authenticate the user");
    }

    conn_ = conn;
    sessionId_ = sessionId;
    isConnected_ = true;
    littleEndian_ = remoteLittleEndian;

    if (!userId_.empty() && encrypted_) {
        try {
            login();
        } catch (...) {
            conn_->close();
            conn_.clear();
            isConnected_ = false;
            throw;
        }
    }
    return true;
}

void DBConnectionImpl::login(const string& userId, const string& password, bool enableEncryption) {
    userId_ = userId;
    pwd_ = password;
    encrypted_ = enableEncryption;
    login();
}

void DBConnectionImpl::login() {
    // TODO: handle the case of encryption.
    vector<ConstantSP> args;
    args.push_back(new String(userId_));
    args.push_back(new String(pwd_));
    args.push_back(new Bool(false));
    ConstantSP result = run("login", args);
    if (!result->getBool())
        throw IOException("Failed to authenticate the user " + userId_);
}

ConstantSP DBConnectionImpl::run(const string& script, int priority, int parallelism, int fetchSize) {
    vector<ConstantSP> args;
    return run(script, "script", args, priority, parallelism, fetchSize);
}

ConstantSP DBConnectionImpl::run(const string& funcName, vector<ConstantSP>& args, int priority, int parallelism, int fetchSize) {
    return run(funcName, "function", args, priority, parallelism, fetchSize);
}

void DBConnectionImpl::upload(const string& name, const ConstantSP& obj) {
    if (!Util::isVariableCandidate(name))
        throw RuntimeException(name + " is not a qualified variable name.");
    vector<ConstantSP> args(1, obj);
    run(name, "variable", args);
}

void DBConnectionImpl::upload(vector<string>& names, vector<ConstantSP>& objs) {
    if (names.size() != objs.size())
        throw RuntimeException("the size of variable names doesn't match the size of objects.");
    if (names.empty())
        return;

    string varNames;
    for (unsigned int i = 0; i < names.size(); ++i) {
        if (!Util::isVariableCandidate(names[i]))
            throw RuntimeException(names[i] + " is not a qualified variable name.");
        if (i > 0)
            varNames.append(1, ',');
        varNames.append(names[i]);
    }
    run(varNames, "variable", objs);
}

ConstantSP DBConnectionImpl::run(const string& script, const string& scriptType, vector<ConstantSP>& args, int priority, int parallelism, int fetchSize) {
    if (!isConnected_)
        throw IOException("Couldn't send script/function to the remote host because the connection has been closed");

    if(fetchSize > 0 && fetchSize < 8192)
        throw IOException("fetchSize must be greater than 8192");
    string body;
    int argCount = args.size();
    if (scriptType == "script")
        body = "script\n" + script;
    else {
        body = scriptType + "\n" + script;
        body.append("\n" + std::to_string(argCount));
        body.append("\n");
        body.append(Util::isLittleEndian() ? "1" : "0");
    }
    string out("API " + sessionId_ + " ");
    out.append(Util::convert((int)body.size()));
    if(asynTask_)
        out.append(" / 4_1_" + std::to_string(priority) + "_" + std::to_string(parallelism));
    else
        out.append(" / 0_1_" + std::to_string(priority) + "_" + std::to_string(parallelism));
    if(fetchSize > 0)
        out.append("__" + std::to_string(fetchSize));
    out.append(1, '\n');
    out.append(body);

    IO_ERR ret;
    if (argCount > 0) {
        for (int i = 0; i < argCount; ++i) {
            if (args[i]->containNotMarshallableObject()) {
                throw IOException("The function argument or uploaded object is not marshallable.");
            }
        }
        DataOutputStreamSP outStream = new DataOutputStream(conn_);
        ConstantMarshallFactory marshallFactory(outStream);
        for (int i = 0; i < argCount; ++i) {
            ConstantMarshall* marshall = marshallFactory.getConstantMarshall(args[i]->getForm());
            if (i == 0)
                marshall->start(out.c_str(), out.size(), args[i], true, ret);
            else
                marshall->start(args[i], true, ret);
            marshall->reset();
            if (ret != OK) {
                isConnected_ = false;
                conn_.clear();
                throw IOException("Couldn't send function argument to the remote host with IO error type " + std::to_string(ret));
            }
        }
        ret = outStream->flush();
        if (ret != OK) {
            isConnected_ = false;
            conn_.clear();
            throw IOException("Failed to marshall code with IO error type " + std::to_string(ret));
        }
    } else {
        size_t actualLength;
        IO_ERR ret = conn_->write(out.c_str(), out.size(), actualLength);
        if (ret != OK) {
            isConnected_ = false;
            conn_.clear();
            throw IOException("Couldn't send script/function to the remote host because the connection has been closed");
        }
    }

    if(asynTask_)
        return nullptr;
    DataInputStreamSP in = new DataInputStream(conn_);
    if (littleEndian_ != (char)Util::isLittleEndian())
        in->enableReverseIntegerByteOrder();

    string line;
    if ((ret = in->readLine(line)) != OK) {
        isConnected_ = false;
        conn_.clear();
        throw IOException("Failed to read response header from the socket with IO error type " + std::to_string(ret));
    }

    vector<string> headers;
    Util::split(line.c_str(), ' ', headers);
    if (headers.size() != 3) {
        isConnected_ = false;
        conn_.clear();
        throw IOException("Received invalid header");
    }
    sessionId_ = headers[0];
    int numObject = atoi(headers[1].c_str());

    if ((ret = in->readLine(line)) != OK) {
        isConnected_ = false;
        conn_.clear();
        throw IOException("Failed to read response message from the socket with IO error type " + std::to_string(ret));
    }

    if (line != "OK") {
        throw IOException(line);
    }

    if (numObject == 0) {
        return new Void();
    }

    short flag;
    if ((ret = in->readShort(flag)) != OK) {
        isConnected_ = false;
        conn_.clear();
        throw IOException("Failed to read object flag from the socket with IO error type " + std::to_string(ret));
    }

    DATA_FORM form = static_cast<DATA_FORM>(flag >> 8);
    DATA_TYPE type = static_cast<DATA_TYPE >(flag & 0xff);
    if(fetchSize > 0 && form == DF_VECTOR && type == DT_ANY)
        return new BlockReader(in);
    ConstantUnmarshallFactory factory(in);
    ConstantUnmarshall* unmarshall = factory.getConstantUnmarshall(form);
    if (!unmarshall->start(flag, true, ret)) {
        unmarshall->reset();
        isConnected_ = false;
        conn_.clear();
        throw IOException("Failed to parse the incoming object with IO error type " + std::to_string(ret));
    }

    ConstantSP result = unmarshall->getConstant();
    unmarshall->reset();
    return result;
}

DBConnection::DBConnection(bool enableSSL, bool asynTask) : conn_(new DBConnectionImpl(enableSSL)), uid_(""), pwd_(""), ha_(false), enableSSL_(enableSSL),asynTask_(asynTask), nodes_(nullptr) {
    DBConnection::initialize();
}

DBConnection::DBConnection(dolphindb::DBConnection&& oth) : conn_(move(oth.conn_)), uid_(move(oth.uid_)), pwd_(move(oth.pwd_)),
		initialScript_(move(oth.initialScript_)), ha_(oth.ha_), enableSSL_(oth.enableSSL_),asynTask_(oth.asynTask_), nodes_(oth.nodes_) {}

DBConnection& DBConnection::operator=(dolphindb::DBConnection&& oth) {
    if (this == &oth) { return *this; }
    conn_ = move(oth.conn_);
    uid_ = move(oth.uid_);
    pwd_ = move(oth.pwd_);
    initialScript_ = move(oth.initialScript_);
    ha_ = oth.ha_;
    nodes_ = oth.nodes_;
    oth.nodes_.clear();
    enableSSL_ = oth.enableSSL_;
    asynTask_ = oth.asynTask_;
    return *this;
}

DBConnection::~DBConnection() {
    close();
}

bool DBConnection::connect(const string& hostName, int port, const string& userId, const string& password, const string& startup,
		bool ha, const vector<string>& highAvailabilitySites) {
    ha_ = ha;
    initialScript_ = startup;
    if (ha_) {
        while (true) {
            if (conn_->connect(hostName, port, userId, password, enableSSL_, asynTask_)) {
                uid_ = userId;
                pwd_ = password;
                break;
            }
            std::cerr << "Connect Failed, retry in one second." << std::endl;
            Thread::sleep(1000);
        }
        if(highAvailabilitySites.empty()) {
            while (true) {
                try {
                    nodes_ = conn_->run("getDataNodes(false)");
                    break;
                } catch (exception& e) {
                    std::cerr << "ERROR getting other dataNodes, exception: " << e.what() << std::endl;
                    Thread::sleep(1000);
                }
            }
        } else {
            nodes_ = Util::createVector(DT_STRING, highAvailabilitySites.size());
            for (size_t i = 0, e = highAvailabilitySites.size(); i != e; ++i) {
                // check legitimacy
                auto is_number = [](const std::string& s) {
                    return !s.empty() &&
                           std::find_if(s.begin(), s.end(), [](char c) { return !isdigit(c); }) == s.end();
                };
                auto v = Util::split(highAvailabilitySites[i], ':');
                if (v.size() != 2 || !is_number(v[1])) {
                    throw RuntimeException("The format of highAvailabilitySite " + highAvailabilitySites[i] +
                                           " is incorrect, should be host:port, e.g. 192.168.1.1:8848");
                }
                int port = std::stoi(v[1]);
                if (port <= 0 || port > 65535) {
                    throw RuntimeException("The format of highAvailabilitySite " + highAvailabilitySites[i] +
                                           " is incorrect, port should be a positive integer less or equal to 65535");
                }
                nodes_->setString(i, highAvailabilitySites[i]);
            }
        }
        if (!initialScript_.empty()) {
            run(initialScript_);
        }
        return true;
    } else {
        bool ok = conn_->connect(hostName, port, userId, password, enableSSL_, asynTask_);
        if (ok && !initialScript_.empty()) {
            run(initialScript_);
        }
        return ok;
    }
}

bool DBConnection::connected() {
    try {
        ConstantSP ret = conn_->run("1+1");
        return !ret.isNull() && (ret->getInt() == 2);
    } catch (exception& e) {
        return false;
    }
}

bool getNewLeader(const string& s, string& host, int& port) {
    string msg{s};
    if (msg.substr(0, 11) == "<NotLeader>") {
        msg = msg.substr(11);
        auto v = Util::split(msg, ':');
        host = v[0];
        port = std::stoi(v[1]);
        return true;
    } else {
        return false;
    }
}

void DBConnection::switchDataNode(const string& err) {
    if (nodes_.isNull())
        return;

    string host;
    int port;
    if (getNewLeader(err, host, port)) {
        int attempt = 0;
        while (true) {
            std::cerr << "Got new leader exception, new leader is " << host << ":" << port << " #attempt=" << attempt++
                      << std::endl;
            try {
                if (conn_->connect(host, port, uid_, pwd_)) {
                    std::cerr << "Switched to node: " << host << ":" << port << std::endl;
                    if (!initialScript_.empty()) {
                        run(initialScript_);
                    }
                    break;
                }
                else{
                    if (attempt >= 10)
                        throw IOException("Failed to connect to host = " + host + ", port = " + std::to_string(port));
                }
            } catch (IOException& ex) {
                std::cerr << "Connect to node " << host << ":" << port << " came across a exception: " << ex.what()
                          << std::endl;
                if (attempt >= 10)
                    throw ex;

                getNewLeader(ex.what(), host, port);
            }
            Util::sleep(100);
        }
    } else {
        for (int i = 0;; ++i, i %= nodes_->size()) {
            string str = nodes_->get(i)->getString();
            vector<string> v = Util::split(str, ':');
            std::cerr << "Trying node: " << str << std::endl;
            try {
                if (conn_->connect(v[0], std::stoi(v[1]), uid_, pwd_)) {
                    std::cerr << "Switched to node: " << str << std::endl;
                    if (!initialScript_.empty()) {
                        run(initialScript_);
                    }
                    break;
                }
            } catch (IOException& ex) {
                std::cerr << "Trying to reconnect " + str + " with error: " + ex.what() + " and will try next node." << std::endl;
                continue;
            }
            Thread::sleep(1000);
        }
    }
}

void DBConnection::login(const string& userId, const string& password, bool enableEncryption) {
    conn_->login(userId, password, enableEncryption);
    uid_ = userId;
    pwd_ = password;
}

ConstantSP DBConnection::run(const string& script, int priority, int parallelism, int fetchSize) {
    if (ha_) {
        try {
            return conn_->run(script);
        } catch (IOException& e) {
            string host;
            int port;
            if (connected() && !getNewLeader(e.what(), host, port)) {
                throw;
            } else {
                string err(e.what());
                for(int i = 0; i < maxRerunCnt_; ++i) {
                    try {
                        if(!connected() || getNewLeader(err, host, port)){
                            switchDataNode(err);
                        }
                        return conn_->run(script);
                    } catch (exception& e) {
                        if(i == maxRerunCnt_ - 1) {
                            throw;
                        }
                        err = e.what();
                        std::cerr << "Exception during rerun: " << e.what() << ", going to rerun for the " << i << " time in 1 second." << std::endl;
                        Thread::sleep(1000);
                    }
                }
            }
        }
    } else {
        return conn_->run(script, priority, parallelism, fetchSize);
    }
}

ConstantSP DBConnection::run(const string& funcName, vector<dolphindb::ConstantSP>& args, int priority, int parallelism, int fetchSize) {
    if (ha_) {
        try {
            return conn_->run(funcName, args, priority, parallelism, fetchSize);
        } catch (IOException& e) {
            string host;
            int port;
            if (connected() && !getNewLeader(e.what(), host, port)) {
                throw;
            } else {
                string err(e.what());
                for(int i = 0; i < maxRerunCnt_; ++i) {
                    try{
                        if(!connected() || getNewLeader(err, host, port)){
                            switchDataNode(err);
                        }
                        return conn_->run(funcName, args, priority, parallelism, fetchSize);
                    }catch(exception& e){
                        if(i == maxRerunCnt_ - 1)
                            throw;
                        err = e.what();
                        std::cerr << "Exception during rerun: " << string(e.what()) << ", going to rerun for the " << i << " time in 1 second." << std::endl;
                        Thread::sleep(1000);
                    }
                }
            }
        }
    } else {
        return conn_->run(funcName, args, priority, parallelism, fetchSize);
    }
}

void DBConnection::upload(const string& name, const ConstantSP& obj) {
    if (ha_) {
        try {
            return conn_->upload(name, obj);
        } catch (IOException& e) {
            if (connected()) {
                throw e;
            } else {
                switchDataNode(e.what());
                return upload(name, obj);
            }
        }
    } else {
        return conn_->upload(name, obj);
    }
}

void DBConnection::upload(vector<string>& names, vector<ConstantSP>& objs) {
    if (ha_) {
        try {
            return conn_->upload(names, objs);
        } catch (IOException& e) {
            if (connected()) {
                throw;
            } else {
                switchDataNode(e.what());
                return upload(names, objs);
            }
        }
    } else {
        return conn_->upload(names, objs);
    }
}

void DBConnection::close() {
    if (conn_) conn_->close();
}

void DBConnection::initialize() {
    DBConnectionImpl::initialize();
}

const std::string& DBConnection::getInitScript() const {
    return initialScript_;
}

void DBConnection::setInitScript(const std::string & script) {
    initialScript_ = script;
}

BlockReader::BlockReader(const DataInputStreamSP& in ) : in_(in), total_(0), currentIndex_(0){
    int rows, cols;
    if(in->readInt(rows) != OK)
        throw IOException("Failed to read rows for data block.");
    if(in->readInt(cols) != OK)
        throw IOException("Faield to read col for data block.");
    total_ = rows * cols;
}

ConstantSP BlockReader::read(){
    if(currentIndex_>=total_)
        return new Void();
    IO_ERR ret;
    short flag;
    if ((ret = in_->readShort(flag)) != OK)
        throw IOException("Failed to read object flag from the socket with IO error type " + std::to_string(ret));

    DATA_FORM form = static_cast<DATA_FORM>(flag >> 8);
    ConstantUnmarshallFactory factory(in_);
    ConstantUnmarshall* unmarshall = factory.getConstantUnmarshall(form);
    if (!unmarshall->start(flag, true, ret)) {
        unmarshall->reset();
        throw IOException("Failed to parse the incoming object with IO error type " + std::to_string(ret));
    }
    ConstantSP result = unmarshall->getConstant();
    unmarshall->reset();
    currentIndex_ ++;
    return result;
}

void BlockReader::skipAll(){
    for(int i = currentIndex_; i < total_; i++)
        read();
}

};    // namespace dolphindb

namespace std {
size_t hash<dolphindb::Guid>::operator()(const dolphindb::Guid & val) const{
    return murmur32_16b(val.bytes());
}
}
