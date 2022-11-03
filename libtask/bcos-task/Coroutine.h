#pragma once

#if __APPLE__ && __clang__
#include <experimental/coroutine>
namespace CO_STD = std::experimental;
#else
#include <coroutine>
namespace CO_STD = std;
#endif