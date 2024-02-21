#include "TransactionMetaDataImpl.h"

bcostars::protocol::TransactionMetaDataImpl::TransactionMetaDataImpl()
  : m_inner([inner = bcostars::TransactionMetaData()]() mutable { return &inner; })
{}
bcostars::protocol::TransactionMetaDataImpl::TransactionMetaDataImpl(
    bcos::crypto::HashType hash, std::string to)
  : TransactionMetaDataImpl()
{
    setHash(hash);
    setTo(std::move(to));
}
bcostars::protocol::TransactionMetaDataImpl::TransactionMetaDataImpl(
    std::function<bcostars::TransactionMetaData*()> inner)
  : m_inner(std::move(inner))
{}
bcos::crypto::HashType bcostars::protocol::TransactionMetaDataImpl::hash() const
{
    auto const& hashBytes = m_inner()->hash;
    if (hashBytes.size() == bcos::crypto::HashType::SIZE)
    {
        bcos::crypto::HashType hash(
            reinterpret_cast<const bcos::byte*>(hashBytes.data()), bcos::crypto::HashType::SIZE);
        return hash;
    }
    return {};
}
void bcostars::protocol::TransactionMetaDataImpl::setHash(bcos::crypto::HashType _hash)
{
    m_inner()->hash.assign(_hash.begin(), _hash.end());
}
std::string_view bcostars::protocol::TransactionMetaDataImpl::to() const
{
    return m_inner()->to;
}
void bcostars::protocol::TransactionMetaDataImpl::setTo(std::string _to)
{
    m_inner()->to = std::move(_to);
}
uint32_t bcostars::protocol::TransactionMetaDataImpl::attribute() const
{
    return m_inner()->attribute;
}
void bcostars::protocol::TransactionMetaDataImpl::setAttribute(uint32_t attribute)
{
    m_inner()->attribute = attribute;
}
std::string_view bcostars::protocol::TransactionMetaDataImpl::source() const
{
    return m_inner()->source;
}
void bcostars::protocol::TransactionMetaDataImpl::setSource(std::string source)
{
    m_inner()->source = std::move(source);
}
const bcostars::TransactionMetaData& bcostars::protocol::TransactionMetaDataImpl::inner() const
{
    return *m_inner();
}
bcostars::TransactionMetaData& bcostars::protocol::TransactionMetaDataImpl::mutableInner()
{
    return *m_inner();
}
bcostars::TransactionMetaData bcostars::protocol::TransactionMetaDataImpl::takeInner()
{
    return std::move(*m_inner());
}
void bcostars::protocol::TransactionMetaDataImpl::setInner(bcostars::TransactionMetaData inner)
{
    *m_inner() = std::move(inner);
}
RANGES::any_view<std::tuple<bcos::bytesConstRef, bool>>
bcostars::protocol::TransactionMetaDataImpl::conflictFields() const
{
    return m_inner()->conflictFields |
           RANGES::views::transform([](const auto& tuple) -> std::tuple<bcos::bytesConstRef, bool> {
               auto& [field, isWrite] = tuple;
               return {bcos::bytesConstRef((const uint8_t*)field.data(), field.size()), isWrite};
           });
}
void bcostars::protocol::TransactionMetaDataImpl::setConflictFields(
    RANGES::any_view<std::tuple<bcos::bytesConstRef, bool>> conflictFields)
{
    m_inner()->conflictFields.clear();
    for (auto const& [field, isWrite] : conflictFields)
    {
        auto& item = m_inner()->conflictFields.emplace_back();
        item.field.assign(field.begin(), field.end());
        item.isWrite = isWrite;
    }
}
