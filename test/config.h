#include "../include/DolphinDB.h"
#include "../include/Util.h"
#include "../include/BatchTableWriter.h"
#include "Streaming.h"
#include <vector>
#include <string>
#include <climits>
#include <thread>
#include <atomic>
#include <cstdio>
#include <random>
#include <sys/time.h>

using namespace dolphindb;
using namespace std;
using std::endl;
using std::cout;
using std::atomic_long;
static string hostName = "115.239.209.223";
static string host1 = "192.168.1.200";
static int port = 28848;
static auto table = "trades";
static vector<int> listenPorts = {18901,18902,18903,18904,18905,18906,18907,18908,18909,18910};
static string alphas = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
static int pass,fail;
DBConnection conn(false, false);
DBConnectionPool pool(hostName, port, 10, "admin", "123456");
static bool assertObj = true;