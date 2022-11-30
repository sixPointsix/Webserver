#include <iostream>
#include "log.h"
#include <unistd.h>

using namespace std;

int main()
{
    int m_close_log = 0;
    Log::get_instance()->init("log", 0, 8192, 200000, 100);
    LOG_DEBUG("%s", "adsasdadsa");

    sleep(5);
}
