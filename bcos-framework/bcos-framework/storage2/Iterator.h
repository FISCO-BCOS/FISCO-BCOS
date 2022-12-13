#pragma once
#include "../storage/Entry.h"
#include <bcos-task/Task.h>
#include <concepts>

namespace bcos::storage2
{

template <class Impl>
class IteratorBase
{
public:
    task::Task<bool> valid() { return impl().impl_valid(); }
    task::Task<void> next() { return impl().impl_next(); }
    task::Task<storage::Entry> value() { return impl().impl_value(); }

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }

    IteratorBase() = default;
};

template <class Impl>
concept Iterator = std::derived_from<Impl, IteratorBase<Impl>>;

}  // namespace bcos::storage2