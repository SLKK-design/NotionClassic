/* build_stamp.cpp — always touched before each build so __DATE__/__TIME__ stay current. */
#include "debug_con.h"

void LogBuildTime(void)
{
    DebugLog("build", __DATE__ " " __TIME__);
}
