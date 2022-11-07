#include <iostream>
#include "log.h"

using namespace std;

int main()
{
    Log::get_instance()->init("log", 8192, 200000, 10);
    LOG_INFO("%s", "xzy20021112");

    sleep(3);
}
