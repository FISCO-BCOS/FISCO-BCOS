hunter_config(bcos-utilities VERSION 3.0.0-rc2-local
    URL https://${URL_BASE}/FISCO-BCOS/bcos-utilities/archive/6ba8a95e0c6c3208c618bb0562d724decb2b7aab.tar.gz
	SHA1 9368c9807c93fd265ed4369c4dea2023cee8a336
    CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON
)

hunter_config(OpenSSL VERSION tassl_1.1.1b_v1.4-local
    URL https://${URL_BASE}/FISCO-BCOS/TASSL-1.1.1b/archive/6a0fddd0eb33433c190c796e5b6d80db4db52810.tar.gz
    SHA1 14f7590e09b54bb71926ad5853343f20893ee536
)

hunter_config(bcos-crypto VERSION 3.0.0-rc3-local
    URL https://${URL_BASE}/FISCO-BCOS/bcos-crypto/archive/2d23aa9e96b02a72c48c7423396f8db6943e9011.tar.gz
	SHA1 463673dfd705c579e5dbb304ba71a23777ef0ec1
    CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON SM2_OPTIMIZE=ON
)

hunter_config(bcos-boostssl
	VERSION 3.0.0-rc2-local
	URL https://${URL_BASE}/FISCO-BCOS/bcos-boostssl/archive/f45eb9275e259f345f6fba7eea320aef2c424d43.tar.gz
	SHA1 3500fe3db939e966c916e2c8b6ba084ce393b21c
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

hunter_config(tarscpp VERSION 3.0.3-7299ad23)

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

# hunter_config(
#     OpenSSL VERSION "tassl_1.1.1b_v1.4-63b60292"
#     URL https://codeload.github.com/FISCO-BCOS/TASSL-1.1.1b/zip/63b602923f924b432774f6b6a2b22c708d5231c8
#     SHA1 d4ffbdc5b29cf437f5f6711cc3d4b35f04b06965
# )

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
