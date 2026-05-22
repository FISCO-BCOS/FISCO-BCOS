#pragma once

#include <bcos-utilities/Common.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bcos::protocol
{

/// EIP-7702 authorization tuple (authority signs chainId, address, nonce with 0x05 domain).
struct Web3AuthorizationEntry
{
    std::string chainIdDec;  // decimal string; "0" means any chain
    std::string addressHex;  // 40-char hex delegation target, no 0x prefix
    std::string nonceDec;    // decimal string
    uint8_t yParity = 0;
    h256 r;
    h256 s;
};

using Web3AuthorizationList = std::vector<Web3AuthorizationEntry>;

/// Maximum EIP-7702 authorization tuples per transaction (defense in depth).
constexpr std::size_t WEB3_EIP7702_MAX_AUTHORIZATION_LIST_ENTRIES = 256;

}  // namespace bcos::protocol
