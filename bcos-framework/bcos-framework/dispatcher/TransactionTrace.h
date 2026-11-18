// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
#pragma once
#include <chrono>
#include <memory>
#include <string>
#include <vector>
namespace bcos::scheduler
{
struct TraceOptions
{
    std::chrono::milliseconds timeout{5000};
    bool onlyTopCall = false;
};
struct CallTrace
{
    std::string type;
    std::string from;
    std::string to;
    std::string value;
    std::string gas;
    std::string gasUsed;
    std::string input;
    std::string output;
    std::string error;
    std::string revertReason;
    std::vector<std::unique_ptr<CallTrace>> calls;
};
}  // namespace bcos::scheduler
