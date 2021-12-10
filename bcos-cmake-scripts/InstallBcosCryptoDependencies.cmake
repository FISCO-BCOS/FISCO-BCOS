hunter_add_package(bcos-crypto)
find_package(wedpr-crypto CONFIG REQUIRED)
find_package(bcos-framework CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED log chrono system filesystem iostreams thread)
find_package(bcos-crypto CONFIG REQUIRED)
get_target_property(BCOS_CRYPTO_INCLUDE bcos-crypto::bcos-crypto INTERFACE_INCLUDE_DIRECTORIES)
include_directories(${BCOS_CRYPTO_INCLUDE}/bcos-crypto)

