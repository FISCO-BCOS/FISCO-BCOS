#pragma once

#include <cstdint>
#include <tuple>
#define SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("SCHEDULER")
namespace bcos::scheduler
{
using ContextID = int64_t;
using Seq = int64_t;
using Context = std::tuple<ContextID, Seq>;

enum SchedulerError
{
    UnknownError = -70000,
    InvalidStatus,
    InvalidBlockNumber,
    InvalidBlocks,
    NextBlockError,
    PrewriteBlockError,
    CommitError,
    RollbackError,
    UnexpectedKeyLockError,
    BatchError,
    DMCError,
    DAGError,
    ExecutorNotEstablishedError,
    fetchGasLimitError,
    Stopped,
    InvalidBlockVersion,
};

inline const uint64_t TRANSACTION_GAS = 3000000000;

}  // namespace bcos::scheduler