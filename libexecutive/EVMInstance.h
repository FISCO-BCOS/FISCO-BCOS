/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file EVMInstance.h
 * @author wheatli
 * @date 2018.8.28
 * @record copy from aleth, this is a vm interface
 */

#pragma once

#include "EVMInterface.h"

#include <libethcore/EVMSchedule.h>

#include <evmc/evmc.h>

namespace dev
{
namespace eth
{
class Result
{
public:
    explicit Result(evmc_result const& _result) : m_result(_result) {}

    ~Result()
    {
        if (m_result.release)
            m_result.release(&m_result);
    }

    Result(Result&& _other) noexcept : m_result(_other.m_result)
    {
        // Disable releaser of the rvalue object.
        _other.m_result.release = nullptr;
    }

    Result(Result const&) = delete;
    Result& operator=(Result const&) = delete;

    evmc_status_code status() const { return m_result.status_code; }
    int64_t gasLeft() const { return m_result.gas_left; }
    bytesConstRef output() const { return {m_result.output_data, m_result.output_size}; }

private:
    evmc_result m_result;
};

/// Translate the EVMSchedule to EVMInstance-C revision.
evmc_revision toRevision(EVMSchedule const& _schedule);

/// The RAII wrapper for an EVMInstance-C instance.
class EVMInstance : public EVMInterface
{
public:
    explicit EVMInstance(evmc_instance* _instance) noexcept;
    ~EVMInstance() { m_instance->destroy(m_instance); }

    EVMInstance(EVMInstance const&) = delete;
    EVMInstance& operator=(EVMInstance) = delete;

    std::shared_ptr<Result> exec(executive::EVMHostContext& _ext, evmc_revision _rev,
        evmc_message* _msg, const uint8_t* _code, size_t _code_size) final;

private:
    /// The VM instance created with EVMInstance-C <prefix>_create() function.
    evmc_instance* m_instance = nullptr;
};

}  // namespace eth
}  // namespace dev
