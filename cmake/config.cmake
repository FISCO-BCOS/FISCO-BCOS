# Note: hunter_config takes effect globally, it is not recommended to set it in bcos-node, otherwise it will affect all projects that rely on bcos-framework
hunter_config(bcos-framework VERSION 3.0.0-local
    URL https://${URL_BASE}/FISCO-BCOS/bcos-framework/archive/ea82f8130badd53427d2015a7fb6cf031d41c888.tar.gz
    SHA1 0277fd505d6a4767bd9a4ab8c4323ea29b7bcf3e
	CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON
)

hunter_config(bcos-boostssl
	VERSION 3.0.0-local
	URL https://${URL_BASE}/FISCO-BCOS/bcos-boostssl/archive/05d550979fb00b68eeaef79c205c7882aeeeaafa.tar.gz
	SHA1 583bc9a047dd0fcadbc94af8139c0237a90a7004
)

hunter_config(bcos-tars-protocol
    VERSION 3.0.0-local
    URL https://${URL_BASE}/FISCO-BCOS/bcos-tars-protocol/archive/93cbc1fe11106642eb12faac151b35d8d36aa924.tar.gz
    SHA1 77496c79ff9b12527506151f01daaac1f49261bb
    CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=ON HUNTER_PACKAGE_LOG_INSTALL=ON URL_BASE=${URL_BASE}
)

hunter_config(bcos-txpool VERSION 3.0.0-local
    URL  https://${URL_BASE}/FISCO-BCOS/bcos-txpool/archive/ee2f6c2834d4852d7c53c9f84697df23d7d5b5a6.tar.gz
    SHA1 1a6eda3f650388f7ece898e5313dc8db7c36c314
    CMAKE_ARGS URL_BASE=${URL_BASE}  HUNTER_KEEP_PACKAGE_SOURCES=ON
)

hunter_config(bcos-pbft VERSION 3.0.0-local-a2a9f7d2
    URL  https://${URL_BASE}/FISCO-BCOS/bcos-pbft/archive/af8323c19be8fe818055c2fb825657c17027f830.tar.gz
    SHA1 b72c3efcfebcc6549de4dc09766a80730ca24f5b
    CMAKE_ARGS URL_BASE=${URL_BASE}  HUNTER_KEEP_PACKAGE_SOURCES=ON
)

hunter_config(bcos-sync VERSION 3.0.0-local-50e0e264
    URL  https://${URL_BASE}/FISCO-BCOS/bcos-sync/archive/f6c509ddf0fe256f6b1c7433a6b8df8811367742.tar.gz
    SHA1 1a115881ce7765f9a5904b3f48f207615f702dcc
    CMAKE_ARGS URL_BASE=${URL_BASE}  HUNTER_KEEP_PACKAGE_SOURCES=ON
)

hunter_config(rocksdb VERSION 6.20.3
	URL  https://${URL_BASE}/facebook/rocksdb/archive/refs/tags/v6.20.3.tar.gz
    SHA1 64e4e6031820026c051d6e2072c0197e3bce1643
    CMAKE_ARGS WITH_TESTS=OFF WITH_GFLAGS=OFF WITH_BENCHMARK_TOOLS=OFF WITH_CORE_TOOLS=OFF
    WITH_TOOLS=OFF PORTABLE=ON FAIL_ON_WARNINGS=OFF WITH_ZSTD=ON BUILD_SHARED_LIBS=OFF
)

hunter_config(bcos-storage VERSION 3.0.0-local
    URL  https://${URL_BASE}/FISCO-BCOS/bcos-storage/archive/ed1b7eceebba55e66bb9c2a314bd1e74994a6b75.tar.gz
    SHA1 375561adef104496301210b2b035d7137494967f
    CMAKE_ARGS URL_BASE=${URL_BASE} HUNTER_KEEP_PACKAGE_SOURCES=ON
)

hunter_config(bcos-ledger
    VERSION 3.0.0-local-1956c515f
    URL  https://${URL_BASE}/FISCO-BCOS/bcos-ledger/archive/6a5a8854c02cfa88ca6ac68214f9459410fb312d.tar.gz
    SHA1 ad29c86848a5977c271a42e05dbaef7e3c5b1cae
    CMAKE_ARGS URL_BASE=${URL_BASE} HUNTER_KEEP_PACKAGE_SOURCES=ONqg
)

hunter_config(bcos-front VERSION 3.0.0-local-2ed687bb
    URL  https://${URL_BASE}/FISCO-BCOS/bcos-front/archive/b27ac074af4ccbc2f8afaa943ce21be00145567b.tar.gz
    SHA1 0ae389934ebf0a2fe9f533f7cc40500164776825
    CMAKE_ARGS URL_BASE=${URL_BASE} HUNTER_KEEP_PACKAGE_SOURCES=ON
)

hunter_config(bcos-gateway VERSION 3.0.0-local
    URL  https://${URL_BASE}/FISCO-BCOS/bcos-gateway/archive/2049c513f1d2328fb54c7a1ca96387e8d29b1d3e.tar.gz
    SHA1 fdef35b899a39e50e91b10930bf50723803b06d8
    CMAKE_ARGS URL_BASE=${URL_BASE} HUNTER_KEEP_PACKAGE_SOURCES=ON
)

hunter_config(bcos-scheduler VERSION 3.0.0-local
    URL  https://${URL_BASE}/FISCO-BCOS/bcos-scheduler/archive/829306653bb80616b6d945cce127c90d2d571edb.tar.gz
    SHA1 dd8666da668c30b5ad4d084667777817d47a2a08
    CMAKE_ARGS URL_BASE=${URL_BASE} HUNTER_KEEP_PACKAGE_SOURCES=ON
)

hunter_config(bcos-rpc VERSION 3.0.0-local
    URL  https://${URL_BASE}/FISCO-BCOS/bcos-rpc/archive/1ae943367d66e4a3b33dfc8e2c269204256ff211.tar.gz
    SHA1 4ea27129b8d815bc2452847b9e1fd02068be7e51
    CMAKE_ARGS URL_BASE=${URL_BASE} HUNTER_KEEP_PACKAGE_SOURCES=ON
)

hunter_config(evmc VERSION 7.3.0-c7feb13f
	URL  https://${URL_BASE}/FISCO-BCOS/evmc/archive/c7feb13f582919242da9f4f898ed4578785c9ecc.tar.gz
	SHA1 28ab1c74dd3340efe101418fd5faf19d34c9f7a9
    CMAKE_ARGS URL_BASE=${URL_BASE}
)

hunter_config(intx VERSION 0.4.1 URL https://${URL_BASE}/chfast/intx/archive/v0.4.0.tar.gz
    SHA1  8a2a0b0efa64899db972166a9b3568a6984c61bc
	CMAKE_ARGS CMAKE_CXX_FLAGS=-std=c++17
)

hunter_config(ethash VERSION 0.7.0-4576af36 URL https://${URL_BASE}/chfast/ethash/archive/4576af36f8ebb9bee2d5f04be692f295c64a7211.tar.gz
	SHA1 2001a265177c722b4cbe91c4160f3f582e0c9938
	CMAKE_ARGS CMAKE_CXX_FLAGS=-std=c++17
)

hunter_config(evmone VERSION 0.4.1-b726a1e1
	URL https://${URL_BASE}/FISCO-BCOS/evmone/archive/b726a1e1722109291ac18bc7c5fad5aac9d2e8c5.tar.gz
	SHA1 e41fe0514a7a49a9a5e7eeb1b42cf2c3ced67f5d
	CMAKE_ARGS CMAKE_CXX_FLAGS=-std=c++17 BUILD_SHARED_LIBS=OFF
)

hunter_config(tarscpp VERSION 3.0.3-7299ad23
	URL https://${URL_BASE}/FISCO-BCOS/TarsCpp/archive/7299ad23830b50ca6284e11bb0374f2670f23cdf.tar.gz
	SHA1 9667c0d775bbbc6400a47034bee86003888db978
)

hunter_config(bcos-crypto VERSION 3.0.0-local
	URL https://${URL_BASE}/FISCO-BCOS/bcos-crypto/archive/d91d770edda01502ed5ee0c8fe1efdd35b06d308.tar.gz
	SHA1 d5390f09ce5ffc590dcc80a27b10f1e0c1242087
	CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=OFF HUNTER_PACKAGE_LOG_INSTALL=ON
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
