#pragma once

#if !__cpp_impl_coroutine
#include <experimental/coroutine>
namespace CO_STD = std::experimental;
#else
#include <coroutine>
namespace CO_STD = std;
#endif