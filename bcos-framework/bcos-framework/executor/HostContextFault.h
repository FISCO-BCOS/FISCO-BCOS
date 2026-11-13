/*
 * Copyright (C) 2026 FISCO BCOS.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdexcept>

namespace bcos::executor
{
/// A node-local execution failure. This must escape transaction receipt mapping.
class HostContextFault : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};
}  // namespace bcos::executor
