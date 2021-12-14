/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file SealerFactory.cpp
 * @author: yujiechen
 * @date: 2021-05-20
 */
#include "SealerFactory.h"
#include "Sealer.h"
using namespace bcos;
using namespace bcos::sealer;

SealerFactory::SealerFactory(bcos::protocol::BlockFactory::Ptr _blockFactory,
    bcos::txpool::TxPoolInterface::Ptr _txpool, unsigned _minSealTime)
  : m_blockFactory(_blockFactory), m_txpool(_txpool), m_minSealTime(_minSealTime)
{}

Sealer::Ptr SealerFactory::createSealer()
{
    auto sealerConfig = std::make_shared<SealerConfig>(m_blockFactory, m_txpool);
    sealerConfig->setMinSealTime(m_minSealTime);
    return std::make_shared<Sealer>(sealerConfig);
}