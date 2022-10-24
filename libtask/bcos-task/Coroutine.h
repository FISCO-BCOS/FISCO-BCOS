#pragma once

#if __APPLE__ && __clang__
#include <experimental/coroutine>
namespace CO_STD = std::experimental;
#else

namespace CO_STD = std;
#endif

namespace bcos::coroutine
{

}  // namespace bcos::coroutine