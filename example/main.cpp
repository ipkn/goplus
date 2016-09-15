#include "../include/goplus.h"
#include <iostream>

using namespace goplus;

int main() 
{
    auto ch = make_chan<int>(1);
    go+ [ch]() mutable{
        ch << 101;
        ch << 203;
    };
    go+ [ch]() mutable{
        ch << 2;
    };

    int x;
    int sum = 0;

    ch >> x;
    sum += x;
    std::cout << x << ' ' << sum << std::endl;

    ch >> x;
    sum += x;
    std::cout << x << ' ' << sum << std::endl;

    ch >> x;
    sum += x;
    std::cout << x << ' ' << sum << std::endl;

    return 0;
}

