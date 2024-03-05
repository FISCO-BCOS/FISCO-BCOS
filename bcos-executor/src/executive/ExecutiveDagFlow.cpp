//
// Created by Jimmy Shi on 2023/1/7.
//

#include "ExecutiveDagFlow.h"
#include "../dag/ClockCache.h"
#include "../dag/ScaleUtils.h"
#include "../vm/Precompiled.h"
#include "TransactionExecutive.h"
#include <bcos-framework/executor/ExecuteError.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>


using namespace bcos;
using namespace bcos::executor;
using namespace bcos::executor::critical;
using namespace std;


void ExecutiveDagFlow::submit(CallParameters::UniquePtr txInput)
{
    assert(m_inputs);

    auto contextID = txInput->contextID;
    auto seq = txInput->seq;
    auto type = txInput->type;
    auto executiveState = m_executives[{contextID, seq}];
    if (executiveState == nullptr)
    {
        DAGFLOW_LOG(DEBUG) << "submit: new dmc tx" << txInput->toString();
        //(*m_inputs)[contextID] = std::move(txInput);
        executiveState = std::make_shared<ExecutiveState>(m_executiveFactory, std::move(txInput));
        m_executives[{contextID, seq}] = executiveState;
    }
    else
    {
        DAGFLOW_LOG(DEBUG) << "submit: resume dmc tx" << txInput->toString();
        // update resume params
        executiveState->setResumeParam(std::move(txInput));
    }

    if (seq == 0 && type == CallParameters::MESSAGE)
    {
        assert(false);  // never goes here, this submit function is just for update waitingFlow
    }
    else
    {
        // the tx is not first run:
        // 1. created by sending from a contract
        // 2. is a revert message, seq = 0 but type = REVERT
        m_pausedPool.erase({contextID, seq});
        m_waitingFlow.insert({contextID, seq});
    }
}

inline bool isResumeInputs(std::shared_ptr<std::vector<CallParameters::UniquePtr>> txInputs)
{
    if (!txInputs)
    {
        return false;
    }

    for (auto& input : *txInputs)
    {
        if (input->seq > 0 || input->type != CallParameters::MESSAGE)  // FINISH/REVERT is also
                                                                       // resume message
        {
            return true;
        }
    }

    return false;
}

void ExecutiveDagFlow::submit(std::shared_ptr<std::vector<CallParameters::UniquePtr>> txInputs)
{
    bcos::RecursiveGuard lock(x_lock);

    if (!isResumeInputs(txInputs))
    {
        if (m_dagFlow)
        {
            // m_dagFlow has been set before this function call, just use it
        }
        else
        {
            // generate m_dagFlow
            m_dagFlow = prepareDagFlow(
                m_executiveFactory->getBlockContext(), m_executiveFactory, *txInputs, m_abiCache);
        }

        assert(!m_inputs);
        m_inputs = txInputs;
    }
    else
    {
        // is dmc resume input
        if (!m_inputs)
        {
            m_inputs = std::make_shared<std::vector<CallParameters::UniquePtr>>();
        }
        for (auto& txInput : *txInputs)
        {
            submit(std::move(txInput));
        }
    }
}

void ExecutiveDagFlow::runOriginFlow(std::function<void(CallParameters::UniquePtr)> onTxReturn)
{
    auto startT = utcTime();
    auto txsSize = m_inputs->size();
    if (!m_inputs || txsSize == 0)
    {
        // clear input data
        m_inputs = nullptr;
        m_dagFlow = nullptr;
        return;
    }

    m_dagFlow->setExecuteTxFunc([this](ID id) {
        DAGFLOW_LOG(DEBUG) << LOG_DESC("Execute tx: start") << LOG_KV("id", id);
        if (!m_isRunning)
        {
            return;
        }

        CallParameters::UniquePtr output;
        bool isDagTx = m_dagFlow->isDagTx(id);

        if (isDagTx) [[likely]]
        {
            auto& input = (*m_inputs)[id];
            DAGFLOW_LOG(TRACE) << LOG_DESC("Execute tx: start DAG tx") << input->toString()
                               << LOG_KV("to", input->receiveAddress)
                               << LOG_KV("data", toHexStringWithPrefix(input->data));

            // dag tx no need to use coroutine
            auto executive = m_executiveFactory->build(
                input->codeAddress, input->contextID, input->seq, ExecutiveType::common);

            output = executive->start(std::move(input));

            DAGFLOW_LOG(DEBUG) << "execute tx finish DAG " << output->toString();
            f_onTxReturn(std::move(output));
        }
        else
        {
            // run normal tx
            auto& input = (*m_inputs)[id];
            DAGFLOW_LOG(DEBUG) << LOG_DESC("Execute tx: start normal tx") << LOG_KV("id", id)
                               << LOG_KV("to", input->receiveAddress);
            DAGFLOW_LOG(TRACE) << LOG_DESC("txData") << input->toString() << LOG_KV("id", id)
                               << LOG_KV("to", input->receiveAddress)
                               << LOG_KV("data", toHexStringWithPrefix(input->data));

            ExecutiveState::Ptr executiveState =
                std::make_shared<ExecutiveState>(m_executiveFactory, std::move(input));

            runOne(executiveState, [&](CallParameters::UniquePtr output) {
                if (output->type == CallParameters::MESSAGE ||
                    output->type == CallParameters::KEY_LOCK)
                {
                    m_executives[{output->contextID, output->seq}] = std::move(executiveState);
                    // call back
                    DAGFLOW_LOG(DEBUG) << "Execute tx: normal externalCall " << output->toString();
                    m_dagFlow->pause();
                }
                else
                {
                    DAGFLOW_LOG(DEBUG) << "Execute tx: normal finish " << output->toString();
                }
                f_onTxReturn(std::move(output));
            });
        }
    });

    f_onTxReturn = std::move(onTxReturn);
    m_dagFlow->run(m_DAGThreadNum);
    f_onTxReturn = nullptr;  // must deconstruct for ptr holder release

    if (m_dagFlow->hasFinished())
    {
        // clear input data
        m_inputs = nullptr;
        m_dagFlow = nullptr;
    }
    DAGFLOW_LOG(INFO) << LOG_DESC("runOriginFlow finish") << LOG_KV("txsSize", txsSize)
                      << LOG_KV("cost", utcTime() - startT);
}

critical::CriticalFieldsInterface::Ptr ExecutiveDagFlow::generateDagCriticals(
    const BlockContext& blockContext, ExecutiveFactory::Ptr executiveFactory,
    std::vector<CallParameters::UniquePtr>& inputs,
    std::shared_ptr<ClockCache<bcos::bytes, FunctionAbi>> abiCache)
{
    auto transactionsNum = inputs.size();
    CriticalFields::Ptr txsCriticals = make_shared<CriticalFields>(transactionsNum);
    if (!g_BCOSConfig.enableDAG())
    {
        DAGFLOW_LOG(DEBUG) << "generateDags: DAG has disabled, just return all conflict"
                           << LOG_KV("transactionsNum", transactionsNum);
        // return an empty vector (all object is nullptr) means every tx are conflict all
        return txsCriticals;
    }
    DAGFLOW_LOG(DEBUG) << "generateDags start" << LOG_KV("transactionsNum", transactionsNum);

    mutex tableMutex;

    // parallel to extract critical fields
    tbb::parallel_for(tbb::blocked_range<uint64_t>(0, transactionsNum),
        [&](const tbb::blocked_range<uint64_t>& range) {
            try
            {
                for (auto i = range.begin(); i != range.end(); ++i)
                {
                    const auto& params = inputs[i];

                    auto to = params->receiveAddress;
                    const auto& input = params->data;

                    CriticalFields::CriticalFieldPtr conflictFields = nullptr;
                    auto selector = ref(input).getCroppedData(0, 4);
                    auto abiKey = bytes(to.cbegin(), to.cend());
                    abiKey.insert(abiKey.end(), selector.begin(), selector.end());
                    // if precompiled
                    auto p = executiveFactory->getPrecompiled(params->receiveAddress);
                    if (p)
                    {
                        // Precompile transaction
                        if (p->isParallelPrecompiled())
                        {
                            DAGFLOW_LOG(TRACE)
                                << "generateDags: isParallelPrecompiled " << LOG_KV("address", to);
                            auto criticals = vector<string>(
                                p->getParallelTag(ref(params->data), blockContext.isWasm()));
                            conflictFields = make_shared<vector<bytes>>();
                            for (string& critical : criticals)
                            {
                                critical += params->receiveAddress;
                                conflictFields->push_back(bytes((uint8_t*)critical.data(),
                                    (uint8_t*)critical.data() + critical.size()));
                            }
                        }
                        else
                        {
                            // Note: must be sure that the log accessed data should be valid
                            // always
                            DAGFLOW_LOG(TRACE)
                                << "generateDags: " << LOG_DESC("the precompiled can't be parallel")
                                << LOG_KV("address", to);
                            txsCriticals->put(i, nullptr);
                            continue;
                        }
                    }
                    else
                    {
                        auto cacheHandle = abiCache->lookup(abiKey);
                        // find FunctionAbi in cache first
                        if (!cacheHandle.isValid())
                        {
                            DAGFLOW_LOG(TRACE) << "generateDags: "
                                               << LOG_DESC("No ABI found in cache, try to load")
                                               << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));

                            std::lock_guard guard(tableMutex);

                            cacheHandle = abiCache->lookup(abiKey);
                            if (cacheHandle.isValid())
                            {
                                DAGFLOW_LOG(TRACE)
                                    << "generateDags: "
                                    << LOG_DESC("ABI had been loaded by other workers")
                                    << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));
                                auto& functionAbi = cacheHandle.value();
                                conflictFields =
                                    extractConflictFields(functionAbi, *params, blockContext);
                            }
                            else
                            {
                                auto storage = blockContext.storage();

                                auto tableName = "/apps/" + string(to);

                                auto table = storage->openTable(tableName);
                                if (!table.has_value())
                                {
                                    DAGFLOW_LOG(TRACE)
                                        << "generateDags: "
                                        << LOG_DESC("No ABI found, please deploy first")
                                        << LOG_KV("tableName", tableName);
                                    txsCriticals->put(i, nullptr);
                                    continue;
                                }
                                // get abi json
                                // new logic
                                std::string_view abiStr;
                                if (blockContext.blockVersion() >=
                                    uint32_t(bcos::protocol::BlockVersion::V3_1_VERSION))
                                {
                                    // get codehash
                                    auto entry = table->getRow(ACCOUNT_CODE_HASH);
                                    if (!entry || entry->get().empty())
                                    {
                                        DAGFLOW_LOG(TRACE)
                                            << "generateDags: "
                                            << "No codeHash found, please deploy first "
                                            << LOG_KV("tableName", tableName);
                                        txsCriticals->put(i, nullptr);
                                        continue;
                                    }

                                    auto codeHash = entry->getField(0);

                                    // get abi according to codeHash
                                    auto abiTable =
                                        storage->openTable(bcos::ledger::SYS_CONTRACT_ABI);
                                    auto abiEntry = abiTable->getRow(codeHash);
                                    if (!abiEntry || abiEntry->get().empty())
                                    {
                                        abiEntry = table->getRow(ACCOUNT_ABI);
                                        if (!abiEntry || abiEntry->get().empty())
                                        {
                                            DAGFLOW_LOG(TRACE)
                                                << "generateDags: "
                                                << "No ABI found, please deploy first "
                                                << LOG_KV("tableName", tableName);
                                            txsCriticals->put(i, nullptr);
                                            continue;
                                        }
                                    }
                                    abiStr = abiEntry->getField(0);
                                }
                                else
                                {
                                    // old logic
                                    auto entry = table->getRow(ACCOUNT_ABI);
                                    abiStr = entry->getField(0);
                                }
                                bool isSmCrypto = blockContext.hashHandler()->getHashImplType() ==
                                                  crypto::HashImplType::Sm3Hash;

                                DAGFLOW_LOG(TRACE) << "generateDags: " << LOG_DESC("ABI loaded")
                                                   << LOG_KV("address", to)
                                                   << LOG_KV("selector", toHexString(selector))
                                                   << LOG_KV("ABI", abiStr);
                                auto functionAbi = FunctionAbi::deserialize(
                                    abiStr, selector.toBytes(), isSmCrypto);
                                if (!functionAbi)
                                {
                                    DAGFLOW_LOG(TRACE)
                                        << "generateDags: " << LOG_DESC("ABI deserialize failed")
                                        << LOG_KV("address", to) << LOG_KV("ABI", abiStr);

                                    // If abi is not valid, we don't impact the cache. In such a
                                    // situation, if the caller invokes this method over and
                                    // over again, executor will read the contract table
                                    // repeatedly, which may cause performance loss. But we
                                    // think occurrence of invalid abi is impossible in actual
                                    // situations.
                                    txsCriticals->put(i, nullptr);
                                    continue;
                                }

                                auto abiPtr = functionAbi.get();
                                if (abiCache->insert(abiKey, abiPtr, &cacheHandle))
                                {
                                    // If abi object had been inserted into the cache
                                    // successfully, the cache will take charge of life time
                                    // management of the object. After this object being
                                    // eliminated, the cache will delete its memory storage.
                                    std::ignore = functionAbi.release();
                                }
                                conflictFields =
                                    extractConflictFields(*abiPtr, *params, blockContext);
                            }
                        }
                        else
                        {
                            DAGFLOW_LOG(TRACE) << "generateDags: " << LOG_DESC("Found ABI in cache")
                                               << LOG_KV("address", to)
                                               << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));
                            auto& functionAbi = cacheHandle.value();
                            conflictFields =
                                extractConflictFields(functionAbi, *params, blockContext);
                        }
                    }
                    if (conflictFields == nullptr)
                    {
                        DAGFLOW_LOG(TRACE)
                            << "generateDags: "
                            << LOG_DESC("The transaction can't be executed concurrently")
                            << LOG_KV("address", to)
                            << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));
                        txsCriticals->put(i, nullptr);
                        continue;
                    }
                    txsCriticals->put(i, std::move(conflictFields));
                }
            }
            catch (exception& e)
            {
                DAGFLOW_LOG(ERROR)
                    << "generateDags: " << LOG_DESC("Error during parallel extractConflictFields")
                    << LOG_KV("EINFO", boost::diagnostic_information(e));
                BOOST_THROW_EXCEPTION(
                    BCOS_ERROR_WITH_PREV(-1, "Error while extractConflictFields", e));
            }
        });
    return txsCriticals;
}


TxDAGFlow::Ptr ExecutiveDagFlow::generateDagFlow(
    const BlockContext& blockContext, critical::CriticalFieldsInterface::Ptr criticals)
{
    auto dagFlow = std::make_shared<TxDAGFlow>();

    dagFlow->init(criticals);
    return dagFlow;
}

std::shared_ptr<std::vector<bytes>> ExecutiveDagFlow::extractConflictFields(
    const FunctionAbi& functionAbi, const CallParameters& params, const BlockContext& _blockContext)
{
    if (functionAbi.conflictFields.empty())
    {
        DAGFLOW_LOG(TRACE) << LOG_BADGE("extractConflictFields")
                           << LOG_DESC("conflictFields is empty")
                           << LOG_KV("address", params.senderAddress)
                           << LOG_KV("functionName", functionAbi.name);
        return nullptr;
    }

    const auto& to = params.receiveAddress;
    auto hasher = boost::hash<string_view>();
    auto toHash = hasher(to);

    auto conflictFields = make_shared<vector<bytes>>();

    for (const auto& conflictField : functionAbi.conflictFields)
    {
        auto criticalKey = bytes();
        criticalKey.reserve(72);

        size_t slot = toHash;
        if (conflictField.slot.has_value())
        {
            slot += static_cast<size_t>(conflictField.slot.value());
        }
        criticalKey.insert(criticalKey.end(), (uint8_t*)&slot, (uint8_t*)&slot + sizeof(slot));
        DAGFLOW_LOG(TRACE) << LOG_BADGE("extractConflictFields") << LOG_KV("to", to)
                           << LOG_KV("functionName", functionAbi.name)
                           << LOG_KV("addressHash", toHash) << LOG_KV("slot", slot);

        switch (conflictField.kind)
        {
        case All:
        {
            DAGFLOW_LOG(TRACE) << LOG_BADGE("extractConflictFields") << LOG_DESC("use `All`");
            return nullptr;
        }
        case Len:
        {
            DAGFLOW_LOG(TRACE) << LOG_BADGE("extractConflictFields") << LOG_DESC("use `Len`");
            conflictFields->emplace_back(std::move(criticalKey));
            break;
        }
        case Env:
        {
            assert(conflictField.value.size() == 1);

            auto envKind = conflictField.value[0];
            switch (envKind)
            {
            case EnvKind::Caller:
            {
                const auto& sender = params.senderAddress;
                criticalKey.insert(criticalKey.end(), sender.begin(), sender.end());

                DAGFLOW_LOG(TRACE) << LOG_BADGE("extractConflictFields") << LOG_DESC("use `Caller`")
                                   << LOG_KV("caller", sender);
                break;
            }
            case EnvKind::Origin:
            {
                const auto& sender = params.origin;
                criticalKey.insert(criticalKey.end(), sender.begin(), sender.end());

                DAGFLOW_LOG(TRACE) << LOG_BADGE("extractConflictFields") << LOG_DESC("use `Origin`")
                                   << LOG_KV("origin", sender);
                break;
            }
            case EnvKind::Now:
            {
                auto now = _blockContext.timestamp();
                auto bytes = static_cast<bcos::byte*>(static_cast<void*>(&now));
                criticalKey.insert(criticalKey.end(), bytes, bytes + sizeof(now));

                DAGFLOW_LOG(TRACE) << LOG_BADGE("extractConflictFields") << LOG_DESC("use `Now`")
                                   << LOG_KV("now", now);
                break;
            }
            case EnvKind::BlkNumber:
            {
                auto blockNumber = _blockContext.number();
                auto bytes = static_cast<bcos::byte*>(static_cast<void*>(&blockNumber));
                criticalKey.insert(criticalKey.end(), bytes, bytes + sizeof(blockNumber));

                DAGFLOW_LOG(TRACE)
                    << LOG_BADGE("extractConflictFields") << LOG_DESC("use `BlockNumber`")
                    << LOG_KV("functionName", functionAbi.name)
                    << LOG_KV("blockNumber", blockNumber);
                break;
            }
            case EnvKind::Addr:
            {
                criticalKey.insert(criticalKey.end(), to.begin(), to.end());

                DAGFLOW_LOG(TRACE) << LOG_BADGE("extractConflictFields") << LOG_DESC("use `Addr`")
                                   << LOG_KV("addr", to);
                break;
            }
            default:
            {
                DAGFLOW_LOG(ERROR) << LOG_BADGE("unknown env kind in conflict field")
                                   << LOG_KV("envKind", envKind);
                return nullptr;
            }
            }
            conflictFields->emplace_back(std::move(criticalKey));
            break;
        }
        case Params:
        {
            assert(!conflictField.value.empty());
            const ParameterAbi* paramAbi = nullptr;
            const auto* components = &functionAbi.inputs;
            auto inputData = ref(params.data).getCroppedData(4).toBytes();
            if (_blockContext.isWasm())
            {
                auto startPos = 0u;
                for (const auto& segment : conflictField.value)
                {
                    if (segment >= components->size())
                    {
                        return nullptr;
                    }

                    for (auto i = 0u; i < segment; ++i)
                    {
                        auto length = scaleEncodingLength(components->at(i), inputData, startPos);
                        if (!length.has_value())
                        {
                            return nullptr;
                        }
                        startPos += length.value();
                    }
                    paramAbi = &components->at(segment);
                    components = &paramAbi->components;
                }
                auto length = scaleEncodingLength(*paramAbi, inputData, startPos);
                if (!length.has_value())
                {
                    return nullptr;
                }
                assert(startPos + length.value() <= inputData.size());
                bytes var(
                    inputData.begin() + startPos, inputData.begin() + startPos + length.value());
                criticalKey.insert(criticalKey.end(), var.begin(), var.end());
            }
            else
            {  // evm
                auto index = conflictField.value[0];
                const auto& typeName = functionAbi.flatInputs[index];
                if (typeName.empty())
                {
                    return nullptr;
                }
                auto out = getComponentBytes(index, typeName, ref(params.data).getCroppedData(4));
                criticalKey.insert(criticalKey.end(), out.begin(), out.end());
            }
            DAGFLOW_LOG(TRACE) << LOG_BADGE("extractConflictFields") << LOG_DESC("use `Params`")
                               << LOG_KV("functionName", functionAbi.name)
                               << LOG_KV("criticalKey", toHexStringWithPrefix(criticalKey));
            conflictFields->emplace_back(std::move(criticalKey));
            break;
        }
        case Const:
        {
            criticalKey.insert(
                criticalKey.end(), conflictField.value.begin(), conflictField.value.end());
            DAGFLOW_LOG(TRACE) << LOG_BADGE("extractConflictFields") << LOG_DESC("use `Const`")
                               << LOG_KV("functionName", functionAbi.name)
                               << LOG_KV("criticalKey", toHexStringWithPrefix(criticalKey));
            conflictFields->emplace_back(std::move(criticalKey));
            break;
        }
        case None:
        {
            DAGFLOW_LOG(TRACE) << LOG_BADGE("extractConflictFields") << LOG_DESC("use `None`")
                               << LOG_KV("functionName", functionAbi.name)
                               << LOG_KV("criticalKey", toHexStringWithPrefix(criticalKey));
            break;
        }
        default:
        {
            DAGFLOW_LOG(ERROR) << LOG_BADGE("unknown conflict field kind")
                               << LOG_KV("conflictFieldKind", conflictField.kind);
            return nullptr;
        }
        }
    }
    return conflictFields;
}
