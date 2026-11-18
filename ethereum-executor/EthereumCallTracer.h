// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
#pragma once
#include <bcos-codec/abi/RevertReason.h>
#include <bcos-framework/dispatcher/TransactionTrace.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <chrono>
#include <evmc/evmc.hpp>
#include <evmone/execution_state.hpp>
#include <evmone/instructions_traits.hpp>
#include <evmone/tracing.hpp>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
namespace bcos::executor_v1::eth
{
/// Request-owned collector. Never attached to the shared consensus VM.
class EthereumCallTracer : public evmone::Tracer
{
public:
    static constexpr size_t maxFrames = 10000;
    static constexpr size_t maxBytes = 8 * 1024 * 1024;
    explicit EthereumCallTracer(scheduler::TraceOptions options)
      : m_deadline(std::chrono::steady_clock::now() + options.timeout),
        m_onlyTop(options.onlyTopCall)
    {}
    bool should_abort() noexcept override
    {
        m_timedOut = m_timedOut || std::chrono::steady_clock::now() >= m_deadline;
        return m_timedOut || m_failed;
    }
    void check()
    {
        if (should_abort())
            throw std::runtime_error(
                m_timedOut ? "execution timeout" : "transaction trace output budget exceeded");
    }
    void setRecording(bool recording) { m_recording = recording; }
    std::shared_ptr<scheduler::CallTrace> result() const { return m_root; }
    static std::string hex(const uint8_t* data, size_t size)
    {
        return bcos::toHex(std::span<const uint8_t>(data, size), "0x");
    }
    static std::string quantity(uint64_t value) { return bcos::toQuantity(value); }
    static std::string quantity(evmc_uint256be const& value)
    {
        return bcos::toQuantity(std::span<const uint8_t>(value.bytes));
    }
    void enter(evmc_message const& msg) noexcept
    {
        if (!m_recording)
            return;
        try
        {
            if (m_onlyTop && !m_stack.empty())
            {
                m_stack.emplace_back();
                return;
            }
            if (++m_frames > maxFrames)
            {
                m_failed = true;
                m_stack.emplace_back();
                return;
            }
            if (!account(msg.input_size + 512))
            {
                m_stack.emplace_back();
                return;
            }
            auto f = std::make_shared<scheduler::CallTrace>();
            bool create = msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2;
            f->type = create ? (msg.kind == EVMC_CREATE ? "CREATE" : "CREATE2") :
                      msg.kind == EVMC_DELEGATECALL ? "DELEGATECALL" :
                      msg.kind == EVMC_CALLCODE     ? "CALLCODE" :
                      (msg.flags & EVMC_STATIC)     ? "STATICCALL" :
                                                      "CALL";
            f->from = hex(msg.sender.bytes, 20);
            f->to = hex(msg.recipient.bytes, 20);
            if (msg.depth > 0)
            {
                auto it = m_pending.find(msg.depth - 1);
                if (it != m_pending.end())
                {
                    f->type = it->second.type;
                    f->from = it->second.from;
                    if (!create)
                        f->to = it->second.to;
                    m_pending.erase(it);
                }
            }
            if (f->type != "STATICCALL")
                f->value = quantity(msg.value);
            f->gas = quantity(static_cast<uint64_t>(msg.gas));
            f->input = hex(msg.input_data, msg.input_size);
            if (m_stack.empty())
                m_root = f;
            else if (m_stack.back())
                m_stack.back()->calls.push_back(f);
            m_stack.push_back(std::move(f));
        }
        catch (...)
        {
            m_failed = true;
        }
    }
    void setError(std::string_view error) noexcept
    {
        if (!m_recording || m_stack.empty() || !m_stack.back())
            return;
        try
        {
            m_stack.back()->error = error;
        }
        catch (...)
        {
            m_failed = true;
        }
    }
    void creationOutput(const uint8_t* data, size_t size) noexcept
    {
        if (size == 0 || !m_recording || m_stack.empty() || !m_stack.back())
            return;
        try
        {
            if (account(size))
                m_stack.back()->output = hex(data, size);
        }
        catch (...)
        {
            m_failed = true;
        }
    }
    void exit(evmc_message const& msg, evmc_result const& result) noexcept
    {
        if (!m_recording || m_stack.empty())
            return;
        auto f = m_stack.back();
        m_stack.pop_back();
        if (!f)
            return;
        try
        {
            f->gasUsed = quantity(static_cast<uint64_t>(msg.gas - result.gas_left));
            if (result.status_code != EVMC_SUCCESS)
            {
                if (f->error.empty())
                    f->error =
                        result.status_code == EVMC_REVERT     ? "execution reverted" :
                        result.status_code == EVMC_OUT_OF_GAS ? "out of gas" :
                        result.status_code == EVMC_INVALID_INSTRUCTION ?
                                                                "invalid opcode" :
                        result.status_code == EVMC_STACK_OVERFLOW ?
                                                                "stack limit reached 1024 (1023)" :
                        result.status_code == EVMC_STACK_UNDERFLOW       ? "stack underflow" :
                        result.status_code == EVMC_STATIC_MODE_VIOLATION ? "write protection" :
                                                                           "execution failed";
                if (msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2)
                    f->to.clear();
                f->output.clear();
            }
            if (result.output_size &&
                (result.status_code == EVMC_SUCCESS || result.status_code == EVMC_REVERT) &&
                account(result.output_size))
            {
                f->output = hex(result.output_data, result.output_size);
                if (result.status_code == EVMC_REVERT)
                    if (auto reason = codec::abi::decodeRevertReason(
                            bytesConstRef(result.output_data, result.output_size));
                        reason && account(reason->size()))
                        f->revertReason = std::move(*reason);
            }
        }
        catch (...)
        {
            m_failed = true;
        }
    }
    void selfdestruct(
        evmc_address const& from, evmc_address const& to, evmc_uint256be const& value) noexcept
    {
        if (!m_recording || m_onlyTop || m_stack.empty() || !m_stack.back())
            return;
        try
        {
            if (++m_frames > maxFrames)
            {
                m_failed = true;
                return;
            }
            if (!account(512))
                return;
            auto f = std::make_shared<scheduler::CallTrace>();
            f->type = "SELFDESTRUCT";
            f->from = hex(from.bytes, 20);
            f->to = hex(to.bytes, 20);
            f->value = quantity(value);
            f->gas = "0x0";
            f->gasUsed = "0x0";
            f->input = "0x";
            m_stack.back()->calls.push_back(std::move(f));
        }
        catch (...)
        {
            m_failed = true;
        }
    }

private:
    bool account(size_t size)
    {
        if (size > (maxBytes - m_bytes) / 2)
        {
            m_failed = true;
            return false;
        }
        m_bytes += size * 2;
        return true;
    }
    struct PendingCall
    {
        std::string type, from, to;
    };
    void notify_call_failure(const evmc_message& message, std::string_view error) noexcept override
    {
        if (!m_recording)
            return;
        enter(message);
        setError(error);
        evmc_result result{};
        result.status_code = EVMC_FAILURE;
        result.gas_left = message.gas;
        exit(message, result);
    }
    void on_execution_start(
        evmc_revision, const evmc_message& msg, evmc::bytes_view) noexcept override
    {
        try
        {
            m_executionDepths.push_back(msg.depth);
            m_pending.erase(msg.depth);
        }
        catch (...)
        {
            m_failed = true;
        }
    }
    void on_execution_end(const evmc_result& result) noexcept override
    {
        if (m_executionDepths.empty())
            return;
        auto depth = m_executionDepths.back();
        m_executionDepths.pop_back();
        if (result.status_code != EVMC_SUCCESS && m_recording && !m_stack.empty() && m_stack.back())
        {
            try
            {
                auto& frame = *m_stack.back();
                auto ins = m_lastInstruction.find(depth);
                if (ins != m_lastInstruction.end())
                {
                    auto [op, height] = ins->second;
                    auto const& traits = evmone::instr::traits[op];
                    switch (result.status_code)
                    {
                    case EVMC_INVALID_INSTRUCTION:
                    case EVMC_UNDEFINED_INSTRUCTION:
                        frame.error = "invalid opcode: " +
                                      (traits.name ? std::string(traits.name) :
                                                     "opcode " + hex(&op, 1) + " not defined");
                        break;
                    case EVMC_STACK_UNDERFLOW:
                        frame.error = "stack underflow (" + std::to_string(height) + " <=> " +
                                      std::to_string(traits.stack_height_required) + ")";
                        break;
                    case EVMC_STACK_OVERFLOW:
                        frame.error =
                            "stack limit reached " + std::to_string(height) + " (" +
                            std::to_string(1024 - std::max<int>(0, traits.stack_height_change)) +
                            ")";
                        break;
                    case EVMC_BAD_JUMP_DESTINATION:
                        frame.error = "invalid jump destination";
                        break;
                    case EVMC_INVALID_MEMORY_ACCESS:
                        frame.error = "return data out of bounds";
                        break;
                    default:
                        break;
                    }
                }
            }
            catch (...)
            {
                m_failed = true;
            }
        }
        m_pending.erase(depth);
        m_lastInstruction.erase(depth);
    }
    void on_instruction_start(uint32_t pc, const intx::uint256* stack, int height, int64_t,
        const evmone::ExecutionState& state) noexcept override
    {
        if (!m_recording || !state.msg || pc >= state.original_code.size())
            return;
        try
        {
            auto op = state.original_code[pc];
            m_lastInstruction[state.msg->depth] = {op, height};
            if ((op == 0xf1 || op == 0xf2 || op == 0xf4 || op == 0xfa) &&
                height >= (op == 0xf1 || op == 0xf2 ? 7 : 6))
            {
                evmc::bytes32 word = intx::be::store<evmc::bytes32>(stack[-1]);
                PendingCall p;
                p.type = op == 0xf1 ? "CALL" :
                         op == 0xf2 ? "CALLCODE" :
                         op == 0xf4 ? "DELEGATECALL" :
                                      "STATICCALL";
                p.from = hex(state.msg->recipient.bytes, 20);
                p.to = hex(word.bytes + 12, 20);
                m_pending[state.msg->depth] = std::move(p);
            }
            else if ((op == 0xf0 || op == 0xf5) && height >= (op == 0xf0 ? 3 : 4))
            {
                PendingCall p;
                p.type = op == 0xf0 ? "CREATE" : "CREATE2";
                p.from = hex(state.msg->recipient.bytes, 20);
                m_pending[state.msg->depth] = std::move(p);
            }
        }
        catch (...)
        {
            m_failed = true;
        }
    }
    std::map<int, PendingCall> m_pending;
    std::map<int, std::pair<uint8_t, int>> m_lastInstruction;
    std::vector<int> m_executionDepths;
    std::chrono::steady_clock::time_point m_deadline;
    bool m_onlyTop = false, m_recording = false, m_timedOut = false, m_failed = false;
    size_t m_frames = 0, m_bytes = 0;
    std::shared_ptr<scheduler::CallTrace> m_root;
    std::vector<std::shared_ptr<scheduler::CallTrace>> m_stack;
};
}  // namespace bcos::executor_v1::eth
