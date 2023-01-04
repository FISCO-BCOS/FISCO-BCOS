#pragma once
#include "Storage.h"

namespace bcos::storage2::rollbackable_storage
{

template <class KeyType, class ValueType>
class RollbackableStorage
{
private:
    std::map<KeyType, ValueType> m_data;

public:
};
}  // namespace bcos::storage2::rollbackable_storage