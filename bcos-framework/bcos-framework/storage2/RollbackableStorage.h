#pragma once
#include "Storage.h"

namespace bcos::storage2::rollbackable_storage
{

template <class KeyType, class ValueType>
class RollbackableStorage : public Storage<RollbackableStorage<KeyType, ValueType>>
{
private:
    std::map<KeyType, ValueType> m_data;
};
}  // namespace bcos::storage2::rollbackable_storage