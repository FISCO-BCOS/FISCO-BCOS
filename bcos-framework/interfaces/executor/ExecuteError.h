#pragma once

namespace bcos
{
namespace executor
{
enum ExecuteError : int32_t
{
    SUCCESS = -80000,
    INVALID_BLOCKNUMBER,
    GETHASH_ERROR,
    CALL_ERROR,
    EXECUTE_ERROR,
    PREPARE_ERROR,
    COMMIT_ERROR,
    ROLLBACK_ERROR,
    DAG_ERROR,
    DEAD_LOCK,
    TABLE_NOT_FOUND,
    STOPPED,
    SCHEDULER_TERM_ID_ERROR  // to notify switch
};
}
}  // namespace bcos
