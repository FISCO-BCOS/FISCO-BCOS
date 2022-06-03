#pragma once
#include "../Concepts.h"

namespace bcos::crypto
{

template <class Key>
concept PrivateKey = TrivialObject<Key>;

template <class Key>
concept PublicKey = TrivialObject<Key>;

// DigestSign CRTP Base
class DigestSign
{
public:
};
}  // namespace bcos::crypto