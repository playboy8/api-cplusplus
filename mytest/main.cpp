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
	char script[1000];
	sprintf(script, "insert into %s values(%s, date(timestamp(%ld)), %lf, %lf, %lf, %lf, %lf, %lf)",tname, "`a", 1546300800000, 1.5+ i++, 0.1+i, 0.1+i, 0.1+i, 0.1+i, 0.1+i);
	conn.run(script);
	return true;
}


int main(int argc, char *argv[]){

	    DBConnection conn;
		try
		{
			/* code */

	        bool ret = conn.connect("192.168.5.185", 8848, "admin", "123456");
		    if(!ret){
			            cout<<"Failed to connect to the server"<<endl;
				            return 0;
					        }
		}
		catch(const std::exception& e)
		{
			std::cerr << e.what() << "  登录错误 \n" ;
		}
		cout<<" connected  to the server"<<endl; 

		TableSP table = createTable();
		conn.upload("myTable", table);

		// insert 
		diff_count  diff_insert;
		diff_insert.start();

		for(int i=0; i < 10000 * 100; i++)
		insert_one_item(conn,"myTable");
		diff_insert.add_snap();

		string script = "select * from myTable;";
		ConstantSP result = conn.run(script);
		cout<<result->getString()<<endl;

		cout<< "totle item size:" << result->size() << std::endl;
		diff_insert.show_diff();


#if 0
		ConstantSP vector = conn.run("`IBM`GOOG`YHOO");
		int size = vector->rows();
			for(int i=0; i<size; ++i)
					cout<<vector->getString(i)<<endl;
#endif

			return 0;
}
