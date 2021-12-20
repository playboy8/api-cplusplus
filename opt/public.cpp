#include "public.h"

#ifdef Windows_Version
#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <direct.h>
#include <shlwapi.h>
#else
#include <sys/timeb.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <string.h>
#endif

bool PUTIL_GetLocalTime(UTIL_TimePtr pTime)
{
    if(nullptr == pTime)
        return false;
    
#ifdef Windows_Version
    SYSTEMTIME stSysTime;
    GetLocalTime(&stSysTime);
    pTime->iYear = stSysTime.wYear;
    pTime->iMon = stSysTime.wMonth;
    pTime->iDay = stSysTime.wDay;
    pTime->iHour = stSysTime.wHour;
    pTime->iMin = stSysTime.wMinute;
    pTime->iSec = stSysTime.wSecond;
    pTime->iWday = stSysTime.wDayOfWeek;
    pTime->iMillisec = stSysTime.wMilliseconds;
#else
    struct timeb stTime = {0,};
    if(0 != ftime(&stTime))
        return false;

    struct  tm stNow = {0,};
    localtime_r(&stTime.time, &stNow);
    
    pTime->iYear = stNow.tm_year +1900;
    pTime->iMon = stNow.tm_mon + 1 ;
    pTime->iDay = stNow.tm_mday;
    pTime->iHour = stNow.tm_hour;
    pTime->iMin = stNow.tm_min;
    pTime->iSec = stNow.tm_sec;
    pTime->iWday = stNow.tm_wday;
    pTime->iMillisec = stTime.millitm;

#endif
    return true;
}


// 将SYSTEMTIME格式转化为 YYYYMMDDHHMMSSMMM
long long PUTIL_SystemTimeToDateTime(UTIL_TimePtr stTime)
{
    return (((((stTime->iYear * 100ll + stTime->iMon) *100 + stTime->iDay) * 100 + stTime->iHour)*100 + stTime->iMin)*100 + stTime->iSec) *1000 + stTime->iMillisec;
}

static long long convert_to_ms(long long k)
{
    long long ms = k % 100000 + ((k / 100000) % 100 + ((k / 10000000) % 100) * 60) *60000;
    return ms;
}

int diff_ms(long long time1, long long time2)
{
    int ms = convert_to_ms(time1) - convert_to_ms(time2);
    return ms;
}


/// 获取当前程序的路劲， 不带最后的\或者/
bool GetModulePath(char* Path, int PathLen)
{   
#ifdef Windows_Version
    DWORD len = GetModuleFileNameA(NULL, Path, PathLen);
    if(0 == nWlen)
    {
        return false;
    }
    PathRemoveFileSpecA(Path);
#else
    if(NULL == getcwd(Path,PathLen))
        return false;
    int nLen = strlen(Path);
    if(nLen >= PathLen)
        return false;

    return true;
#endif
}



diff_count::diff_count(/* args */)
{
}

diff_count::~diff_count()
{
}

bool diff_count::start()
{
    timestap.clear();
    PUTIL_GetLocalTime(&time);
	long long time1 = PUTIL_SystemTimeToDateTime(&time);
    timestap.push_back(time1);
}
bool diff_count::add_snap()
{
    PUTIL_GetLocalTime(&time);
	long long time1 = PUTIL_SystemTimeToDateTime(&time);
    timestap.push_back(time1);
}
int diff_count::show_diff()
{
    int diff = diff_ms(timestap.back(),timestap.front());
    std::cout << " delay ms："  << diff << std::endl;
}


