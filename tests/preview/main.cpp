#include "Coroutine.h"
#include <iostream>

using namespace bcos::storage::V2;

Coroutine getRow()
{
    std::cout << "[getRow] I am coroutine!" << std::endl;
    std::cout << "[getRow] Going to do some async operator!" << std::endl;
    // do some real async operator, async_read_some(...)

    co_yield 100;

    std::cout << "[getRow] Resume from yield!" << std::endl;
    std::cout << "[getRow] Goint to do another async operator!" << std::endl;
    // do some real async operator, async_write_some(...)

    co_yield 200;

    std::cout << "[getRow] Resume from yield again!" << std::endl;
    std::cout << "[getRow] I am over!" << std::endl;

    co_return;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
    std::cout << "[Main] Start call!" << std::endl;
    // auto coroutine = getRow();
    auto coroutine = getRow();

    // Start the coroutine
    while (!coroutine.done())
    {
        std::cout << "[Main] Resume the coroutine!" << std::endl;
        coroutine.resume();
    }

    std::cout << "[Main] End call!" << std::endl;

    return 0;
}