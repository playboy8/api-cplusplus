#include "DolphinDB.h"
#include "Util.h"
#include <iostream>
#include <string>
#include "../opt/public.h"
#include <thread>
using namespace dolphindb;
using namespace std;


TableSP createTable(){
    vector<string> colNames = {"name", "date","price","open","high","low","close","vol"};
    vector<DATA_TYPE> colTypes = {DT_STRING, DT_DATE, DT_DOUBLE, DT_DOUBLE, DT_DOUBLE, DT_DOUBLE, DT_DOUBLE, DT_DOUBLE};
    int colNum = colNames.size();
	int rowNum = 1;
	int indexCapacity=200* 10000;
    ConstantSP table = Util::createTable(colNames, colTypes, rowNum, indexCapacity);
    vector<VectorSP> columnVecs;
    for(int i = 0; i < colNum; ++i)
        columnVecs.push_back(table->getColumn(i));

    for(unsigned int i = 0; i < rowNum; ++i){
        columnVecs[0]->set(i, Util::createString("name_"+std::to_string(i)));
        columnVecs[1]->set(i, Util::createDate(2010, 1, i+1));
        columnVecs[2]->set(i, Util::createDouble(1.0));
		columnVecs[3]->set(i, Util::createDouble(1.0));
		columnVecs[4]->set(i, Util::createDouble(1.0));
		columnVecs[5]->set(i, Util::createDouble(1.0));
		columnVecs[6]->set(i, Util::createDouble(1.0));
		columnVecs[7]->set(i, Util::createDouble(1.0));
    }
    return table;
}

bool insert_one_item(DBConnection& conn, const char* tname)
{
	static int i=0;
	char script[1000];  //"tableInsert{objByName(`st1)}"
	int colNum = 22, rowNum = 1, indexCapacity = 1;
	//Constant val;
	//val.setBinary()
	sprintf(script, "insert into %s values(%s, date(timestamp(%ld)), %lf, %lf, %lf, %lf, %lf, %lf)",tname, "`a", 1546300800000, 1.5+ i++, 0.1+i, 0.1+i, 0.1+i, 0.1+i, 0.1+i);
	conn.run(script);
	return true;
}

 


TableSP createDemoTable(long rows) {
  vector<string> colNames = {
      "id",      "cbool",     "cchar",      "cshort",    "cint",
      "clong",   "cdate",     "cmonth",     "ctime",     "cminute",
      "csecond", "cdatetime", "ctimestamp", "cnanotime", "cnanotimestamp",
      "cfloat",  "cdouble",   "csymbol",    "cstring",   "cuuid",
      "cip",     "cint128"};

  vector<DATA_TYPE> colTypes = {
      DT_LONG,   DT_BOOL,     DT_CHAR,      DT_SHORT,    DT_INT,
      DT_LONG,   DT_DATE,     DT_MONTH,     DT_TIME,     DT_MINUTE,
      DT_SECOND, DT_DATETIME, DT_TIMESTAMP, DT_NANOTIME, DT_NANOTIMESTAMP,
      DT_FLOAT,  DT_DOUBLE,   DT_SYMBOL,    DT_STRING,   DT_UUID,
      DT_IP,     DT_INT128};
  int colNum = 22, rowNum = rows, indexCapacity = rows;
  ConstantSP table =
      Util::createTable(colNames, colTypes, rowNum, indexCapacity);
  vector<VectorSP> columnVecs;
  for (int i = 0; i < colNum; i++)
    columnVecs.push_back(table->getColumn(i));
  unsigned char ip[16] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
  for (int i = 0; i < rowNum; i++) {
    columnVecs[0]->setLong(i, i);
    columnVecs[1]->setBool(i, i % 2);
    columnVecs[2]->setChar(i, i);
    columnVecs[3]->setShort(i, i);
    columnVecs[4]->setInt(i, i);
    columnVecs[5]->setLong(i, i);
    columnVecs[6]->set(i, Util::parseConstant(DT_DATE, "2020.01.01"));
    columnVecs[7]->setInt(i, 24240); // 2020.01M
    columnVecs[8]->setInt(i, i);
    columnVecs[9]->setInt(i, i);
    columnVecs[10]->setInt(i, i);
    columnVecs[11]->setInt(i, 1577836800 + i);      // 2020.01.01 00:00:00+i
    columnVecs[12]->setLong(i, 1577836800000l + i); // 2020.01.01 00:00:00+i
    columnVecs[13]->setLong(i, i);
    columnVecs[14]->setLong(i, 1577836800000000000l +i); // 2020.01.01 00:00:00.000000000+i
    columnVecs[15]->setFloat(i, i);
    columnVecs[16]->setDouble(i, i);
    columnVecs[17]->setString(i, "sym" + to_string(i));
    columnVecs[18]->setString(i, "abc" + to_string(i));
    ip[15] = i;
    columnVecs[19]->setBinary(i, 16, ip);
    columnVecs[20]->setBinary(i, 16, ip);
    columnVecs[21]->setBinary(i, 16, ip);
  }
  return table;
}


bool insert_one_item(DBConnection& conn)
{
	TableSP table = createDemoTable(1);
	vector<ConstantSP> args;
    args.push_back(table);
	//conn.run("tableInsert{objByName(`st1)}",args);
	conn.run("tableInsert{objByName(`st1)}", args);
	args.clear();
	return true;
}

void test_mem_table()
{
	    DBConnection conn;
		try
		{
			/* code */

	        bool ret = conn.connect("192.168.5.185", 8848, "admin", "123456");
		    if(!ret){
			            cout<<"Failed to connect to the server"<<endl;
				            return ;
					        }
		}
		catch(const std::exception& e)
		{
			std::cerr << e.what() << "  登录错误 \n" ;
		}
		cout<<" connected  to the server"<<endl; 



		// insert 
		diff_count  diff_insert;
		diff_insert.start();

		for(int i=0; i < 10000 * 100; i++)
		insert_one_item(conn,"st1");
		diff_insert.add_snap();

		string script = "select * from myTable;";
		ConstantSP result = conn.run(script);
		cout<<result->getString()<<endl;

		cout<< "totle item size:" << result->size() << std::endl;
		diff_insert.show_diff();

}



void test_stream_table()
{
	    DBConnection conn;
		try
		{
			/* code */

	        bool ret = conn.connect("192.168.5.185", 8848, "admin", "123456");
		    if(!ret){
			            cout<<"Failed to connect to the server"<<endl;
				            return ;
					        }
		}
		catch(const std::exception& e)
		{
			std::cerr << e.what() << "  登录错误 \n" ;
		}
		cout<<" connected  to the server"<<endl; 

	//	TableSP table = createTable();
	//	conn.upload("myTable", table);

		// insert 
		diff_count  diff_insert;
		diff_insert.start();

		for(int i=0; i < 10000 * 100; i++)
		insert_one_item(conn);
		diff_insert.add_snap();

		string script = "select * from st1;";
		ConstantSP result = conn.run(script);
		cout<<result->getString()<<endl;

		cout<< "totle item size:" << result->size() << std::endl;
		diff_insert.show_diff();

}


int main(int argc, char *argv[]){

	test_stream_table();

			return 0;
}
