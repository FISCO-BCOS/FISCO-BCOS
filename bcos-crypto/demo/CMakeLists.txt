find_package(ZLIB REQUIRED)

add_executable(perf_demo perf_demo.cpp)
target_link_libraries(perf_demo PRIVATE bcos-crypto Boost::log tassl::crypto ZLIB::ZLIB)

if(ENABLE_IPPCRYPTO)
    find_package(IPPCrypto REQUIRED MODULE)
    if (NOT IPPCRYPTO_FOUND)
        message(FATAL_ERROR "No Intel IPP Cryptography library found on the system. To build bcos-crypto, please specify -DIPPCRYPTO_ROOT_DIR=<path> option, where <path> is the path to directory that contains include/ and lib/ folders of Intel IPP Cryptography product.")
    endif()

    add_executable(hasher_test hasher_test.cpp)
    target_include_directories(hasher_test PUBLIC ${IPPCRYPTO_INCLUDE_DIRS})
    target_link_libraries(hasher_test PUBLIC ${BCOS_CRYPTO_TARGET} Boost::thread tassl::crypto ${IPPCRYPTO_LIBRARIES} ZLIB::ZLIB)
endif()