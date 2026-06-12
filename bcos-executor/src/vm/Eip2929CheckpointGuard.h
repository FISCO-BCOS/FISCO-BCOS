/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief RAII guard for EIP-2929 nested CALL/CREATE checkpoint commit or rollback.
 * @file Eip2929CheckpointGuard.h
 */

#pragma once

#include "Eip2929AccessState.h"
#include <memory>

namespace bcos::executor
{

/// Pushes an EIP-2929 checkpoint on construction; commits on `commit()`, rolls back on destruction
/// unless committed.
struct Eip2929CheckpointGuard
{
    std::shared_ptr<Eip2929AccessState> state;
    bool committed{false};

    explicit Eip2929CheckpointGuard(std::shared_ptr<Eip2929AccessState> accessState)
      : state(std::move(accessState))
    {
        if (state)
        {
            state->pushCheckpoint();
        }
    }

    void commit()
    {
        if (state && !committed)
        {
            state->commitCheckpoint();
            committed = true;
        }
    }

    ~Eip2929CheckpointGuard()
    {
        if (state && !committed)
        {
            state->rollbackCheckpoint();
        }
    }
};

}  // namespace bcos::executor
