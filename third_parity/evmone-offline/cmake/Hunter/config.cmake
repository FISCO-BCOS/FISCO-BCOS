# evmone: Fast Ethereum Virtual Machine implementation
# Copyright 2018-2019 The evmone Authors.
# Licensed under the Apache License, Version 2.0.

hunter_config(
    intx
    VERSION 0.4.0
    URL https://github.com/chfast/intx/archive/v0.4.0.tar.gz
    SHA1 8a2a0b0efa64899db972166a9b3568a6984c61bc
)

hunter_config(
    GTest
    VERSION 1.8.1
    URL https://github.com/google/googletest/archive/release-1.8.1.tar.gz
    SHA1 152b849610d91a9dfa1401293f43230c2e0c33f8
    CMAKE_ARGS BUILD_GMOCK=OFF gtest_force_shared_crt=ON
)
