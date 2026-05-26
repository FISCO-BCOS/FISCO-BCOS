#pragma once

#include <bcos-utilities/Common.h>
#include <string>
#include <utility>
#include <vector>

namespace bcos::protocol
{

/// EIP-2930 access list entry (account 40-hex without 0x prefix + 32-byte storage keys).
struct Web3AccessListEntry
{
    std::string accountHex;
    std::vector<h256> storageKeys;
};

using Web3AccessList = std::vector<Web3AccessListEntry>;

}  // namespace bcos::protocol
