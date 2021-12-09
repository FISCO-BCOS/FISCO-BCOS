hunter_config(Protobuf
VERSION 3.14.0-4a09d77-p0-local
URL "https://${URL_BASE}/cpp-pm/protobuf/archive/v3.14.0-4a09d77-p0.tar.gz"
SHA1 3553ff3bfd7d0c4c1413b1552064b3dca6fa213e
CMAKE_ARGS protobuf_BUILD_TESTS=OFF
)

# hunter_config(wedpr-crypto VERSION 1.2.0-995589bd
# 	URL https://${URL_BASE}/WeBankBlockchain/WeDPR-Lab-Crypto/archive/995589bd768d6d70c200c29541fca6714199f8b0.tar.gz
# 	SHA1 f33b763c1b023965a9d199e288bf503a30293b8c
# 	CMAKE_ARGS HUNTER_PACKAGE_LOG_BUILD=OFF HUNTER_PACKAGE_LOG_INSTALL=ON
# )

hunter_config(Microsoft.GSL
VERSION 2.0.0-p0-local
URL "https://${URL_BASE}/hunter-packages/Microsoft.GSL/archive/v2.0.0-p0.tar.gz"
SHA1 a94c9c1e41edf787a1c080b7cab8f2f4217dbc4b
CMAKE_ARGS GSL_TEST=OFF
)