#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <vector>

namespace bcos::protocol
{

/// EIP-2930 access list entry: 20-byte address + 32-byte storage keys.
struct Web3AccessListEntry
{
    bcos::Address account;
    std::vector<h256> storageKeys;
};

using Web3AccessList = std::vector<Web3AccessListEntry>;

}  // namespace bcos::protocol
