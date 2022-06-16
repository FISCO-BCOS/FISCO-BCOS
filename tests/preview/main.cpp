
#include "StorageInterface2.h"
#include <iostream>

bcos::storage::V2::Coroutine<std::string> callStorage()
{
    bcos::storage::V2::Storage storage;

    std::cout << "Prepare key done!" << std::endl;
    auto key = std::tuple{std::string_view{"table"}, std::string_view{"key"}};
    auto str = co_await storage.getRow(key);
    std::cout << "Str is: " << str << std::endl;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
    std::cout << "Start call!" << std::endl;
    callStorage();
    std::cout << "End call!" << std::endl;

    return 0;
}