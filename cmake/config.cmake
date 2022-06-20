hunter_config(bcos-utilities VERSION 1.0.0-rc3-local
    URL https://${URL_BASE}/FISCO-BCOS/bcos-utilities/archive/632cbeb0412d6ffa3c5217630fe6039f4092f3a4.tar.gz
	SHA1 37b46cd3174ec1b66e28f19e6e8f31f209b61e4b
    CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON
)

hunter_config(bcos-crypto VERSION 1.0.0-rc3-local
    URL https://${URL_BASE}/FISCO-BCOS/bcos-crypto/archive/34ca9b28f3d2f31948c118a70523df6fa4695e23.tar.gz
	SHA1 64e7b64652ccad212a497d9247fd238fea75578a
    CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON
)

hunter_config(bcos-boostssl
    VERSION 1.0.0-rc4-local
    URL https://${URL_BASE}/FISCO-BCOS/bcos-boostssl/archive/1e02d74bd4bdebd6bada02117e92df892e65a53c.tar.gz
    SHA1 55d6421eb8001dca0067881f3c16d04cef8f6596
    CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON ARCH_NATIVE=${ARCH_NATIVE} URL_BASE=${URL_BASE}
)

hunter_config(rocksdb VERSION 6.20.3
	URL  https://${URL_BASE}/facebook/rocksdb/archive/refs/tags/v6.20.3.tar.gz
    SHA1 64e4e6031820026c051d6e2072c0197e3bce1643
    CMAKE_ARGS WITH_TESTS=OFF WITH_GFLAGS=OFF WITH_BENCHMARK_TOOLS=OFF WITH_CORE_TOOLS=OFF
    WITH_TOOLS=OFF PORTABLE=ON FAIL_ON_WARNINGS=OFF WITH_ZSTD=ON BUILD_SHARED_LIBS=OFF
)

hunter_config(evmc VERSION 9.0.0-2e8bc3a6
	URL  https://${URL_BASE}/FISCO-BCOS/evmc/archive/2e8bc3a6fc9c3cdbf5440b329dcdf0753faee43c.tar.gz
	SHA1 51104be45aa5d87363700c2cfde132a1c0691043
    CMAKE_ARGS URL_BASE=${URL_BASE}
)

hunter_config(intx VERSION 0.6.0 URL https://${URL_BASE}/chfast/intx/archive/v0.6.0.tar.gz
    SHA1  507827495de07412863349bc8c2a8704c7b0e5d4
	CMAKE_ARGS CMAKE_CXX_FLAGS=-std=c++17
)

hunter_config(ethash VERSION 0.7.0 URL https://${URL_BASE}/chfast/ethash/archive/v0.7.0.tar.gz
	SHA1 83768c203c98dff1829f038fde98a7226e1edd98
	CMAKE_ARGS CMAKE_CXX_FLAGS=-std=c++17
)

hunter_config(evmone VERSION 8.2.0-53ff1c54
	URL https://${URL_BASE}/FISCO-BCOS/evmone/archive/53ff1c54a2ee5ebcc499586da62ac6e1bb8735cd.tar.gz
	SHA1 e6c1a8f1acd908c770426bb5015d45b3f9138179
	CMAKE_ARGS CMAKE_CXX_FLAGS=-std=c++17 BUILD_SHARED_LIBS=OFF
)
hunter_config(
    Boost VERSION "1.76.0-local"
    URL
    "https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/FISCO-BCOS/deps/boost_1_76_0.tar.bz2
    https://downloads.sourceforge.net/project/boost/boost/1.76.0/source/boost_1_76_0.tar.bz2
    https://nchc.dl.sourceforge.net/project/boost/boost/1.76.0/boost_1_76_0.tar.bz2"
    SHA1
    8064156508312dde1d834fec3dca9b11006555b6
    CMAKE_ARGS
    CONFIG_MACRO=BOOST_UUID_RANDOM_PROVIDER_FORCE_POSIX
)

hunter_config(
    Protobuf VERSION "3.14.0-4a09d77-p0-local"
    URL
    "https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/deps/protobuf/v3.14.0-4a09d77-p0.tar.gz
    https://github.com/cpp-pm/protobuf/archive/v3.14.0-4a09d77-p0.tar.gz"
    SHA1
    3553ff3bfd7d0c4c1413b1552064b3dca6fa213e
)

hunter_config(
    Microsoft.GSL VERSION "2.0.0-p0-local"
    URL
    "https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/deps/Microsoft.GSL/v2.0.0-p0.tar.gz
    https://github.com/hunter-packages/Microsoft.GSL/archive/v2.0.0-p0.tar.gz"
    SHA1
    a94c9c1e41edf787a1c080b7cab8f2f4217dbc4b
)

hunter_config(
    jsoncpp VERSION "1.8.0-local"
    URL
    "https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/deps/jsoncpp/1.8.0.tar.gz
    https://github.com/open-source-parsers/jsoncpp/archive/1.8.0.tar.gz"
    SHA1
    40f7f34551012f68e822664a0b179e7e6cac5a97
)
hunter_config(tbb VERSION 2021.3.0 CMAKE_ARGS BUILD_SHARED_LIBS=OFF)
hunter_config(ZLIB VERSION ${HUNTER_ZLIB_VERSION} CMAKE_ARGS CMAKE_POSITION_INDEPENDENT_CODE=TRUE)
hunter_config(c-ares VERSION 1.14.0-p0 CMAKE_ARGS CMAKE_POSITION_INDEPENDENT_CODE=TRUE CARES_BUILD_TOOLS=OFF)
hunter_config(re2 VERSION ${HUNTER_re2_VERSION} CMAKE_ARGS CMAKE_POSITION_INDEPENDENT_CODE=TRUE RE2_BUILD_TESTING=OFF)
hunter_config(abseil VERSION ${HUNTER_abseil_VERSION} CMAKE_ARGS CMAKE_CXX_FLAGS=-std=c++11 CMAKE_POSITION_INDEPENDENT_CODE=TRUE ABSL_ENABLE_INSTALL=ON ABSL_RUN_TESTS=OFF)
hunter_config(gRPC VERSION ${HUNTER_gRPC_VERSION} CMAKE_ARGS CMAKE_CXX_FLAGS=-std=c++17 CMAKE_POSITION_INDEPENDENT_CODE=TRUE)

hunter_config(OpenSSL VERSION tassl_1.1.1b_v1.4-local
    URL https://${URL_BASE}/FISCO-BCOS/TASSL-1.1.1b/archive/58c6dbd7599eae60cf547e51d6ace4cd7bff2255.tar.gz
    SHA1 df96f97d027c6c2b2edf7e908bd23925cf061dba
)

hunter_config(tarscpp VERSION 3.0.4-local
    URL https://${URL_BASE}/FISCO-BCOS/TarsCpp/archive/5ef1e21daaf1e143e81be5c7560c879f76edf447.tar.gz
	SHA1 000a070a99d82740f2f238f2defbc2ee7ff3bf76
)

hunter_config(etcd-cpp-apiv3 VERSION 0.2.5-local
    URL "https://${URL_BASE}/FISCO-BCOS/etcd-cpp-apiv3/archive/704a0ea5ea4aeddc28f7d921135b6c34d00f06f1.tar.gz"
    SHA1 3f2efe0ae536997d2dcaf2b253b6d7a35f731dba
)