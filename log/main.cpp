#include <iostream>
#include "log.h"

using namespace std;

int main()
{
    Log::get_instance()->init("log", 8192, 200000);
    LOG_ERROR("%s", "xzy20021112");

}
