#pragma once
#include "../TrivialObject.h"
#include <concepts>
#include <span>
#include <type_traits>

namespace bcos::crypto::digestsign
{

template <class DigestSignType>
concept DigestSign = requires(DigestSignType digestSign)
{
    typename DigestSignType::Key;
    typename DigestSignType::Sign;

    digestSign.sign(typename DigestSignType::Key const& key, trivial::Object auto const& hash);
};

}  // namespace bcos::crypto::digestsign