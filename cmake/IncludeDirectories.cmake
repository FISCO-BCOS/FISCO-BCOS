# Each module now exposes its headers via target_include_directories(PUBLIC ...).
# The root include is kept because cross-module private-header includes
# (e.g. bcos-scheduler/src/..., bcos-table/src/..., bcos-executor/src/...)
# and <concepts/bcos-concepts/...> style includes still depend on it.
#
# Core modules with circular/mutual header dependencies that cannot be
# expressed purely through target_link_libraries:
#   bcos-crypto, bcos-framework, bcos-tool,
#   bcos-sync, bcos-rpbft, bcos-pbft
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/bcos-crypto)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/bcos-framework)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/bcos-tool)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/bcos-sync)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/bcos-rpbft)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/bcos-pbft)

# NOTE: The following global include_directories have been removed because
# each module's CMake target now properly uses target_include_directories(PUBLIC)
# and the necessary target_link_libraries have been added:
# - bcos-txpool (incl. binary dir), bcos-front, bcos-sealer,
#   bcos-gateway, bcos-rpc, bcos-security, bcos-utilities,
#   bcos-protocol, bcos-pbft