#pragma once
#include <utility>

namespace bcos
{

inline constexpr auto recursiveLambda(auto&& lambda)
{
    return [lambdaImpl = std::forward<decltype(lambda)>(lambda)](auto&&... args) -> decltype(auto) {
        return lambdaImpl(lambdaImpl, std::forward<decltype(args)>(args)...);
    };
};

}  // namespace bcos