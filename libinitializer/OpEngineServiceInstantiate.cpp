/**
 * Production instantiation point for OpEngineService template definitions.
 *
 * OpEngineService.h is declarations-only; OpEngineService.inl is opt-in (see
 * engine/CMakeLists.txt). libinitializer wires the concrete MemPoolImpl +
 * GlobalStateStorage + OpSchedulerSeam specialization used by Initializer::init.
 */
#include "GlobalStateStorageInitializer.h"
#include "bcos-mempool/MemPoolImpl.h"
#include <opstack-executor/OpSchedulerSeam.h>
#include <engine/bcos-engine/OpEngineService.inl>

template class bcos::engine::OpEngineService<bcos::txpool::MemPoolImpl,
    bcos::initializer::GlobalStateStorage,
    bcos::evm::engine::OpSchedulerSeam<bcos::initializer::GlobalStateStorage::ViewType>>;
