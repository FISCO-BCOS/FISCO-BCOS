hunter_add_package(bcos-executor)
hunter_add_package(bcos-crypto)
hunter_add_package(jsoncpp)

hunter_add_package(evmc)
find_package(evmc CONFIG REQUIRED)
link_directories("${BCOS-EXECUTOR_ROOT}/lib")

find_package(bcos-framework CONFIG REQUIRED)
find_package(wedpr-crypto CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED serialization)
find_package(bcos-executor CONFIG REQUIRED)
