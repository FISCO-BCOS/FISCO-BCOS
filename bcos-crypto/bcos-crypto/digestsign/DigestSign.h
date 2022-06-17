#pragma once
#include "../TrivialObject.h"

namespace bcos::crypto
{

template <class Key>
concept PrivateKey = requires(Key key)
{
    TrivialObject<decltype(key.value)>;
    typename Key::type;
};

template <class Key>
concept PublicKey = TrivialObject<Key>;

// DigestSign CRTP Base
class DigestSign
{
public:
};
}  // namespace bcos::crypto