#pragma once

#include <bcos-tars-protocol/tars/Block.h>

namespace bcostars::protocol::impl
{
class Block : public bcostars::Block
{
public:
    std::vector<std::byte> encode() const {}
};
};  // namespace bcostars::protocol::impl