# Install script for directory: /Users/chenquan/Workspace/cpp/FISCO-BCOS

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libchannelserver/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libdevcore/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libdevcrypto/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libethcore/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libstat/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libflowlimit/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libtxpool/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libstorage/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libprecompiled/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libnetwork/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libp2p/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libexecutive/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libmptstate/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libblockverifier/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libstoragestate/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libblockchain/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libsync/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libconsensus/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libledger/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/librpc/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libinitializer/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libsecurity/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/libeventfilter/cmake_install.cmake")
  include("/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/fisco-bcos/cmake_install.cmake")

endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "/Users/chenquan/Workspace/cpp/FISCO-BCOS/cmake-build-debug/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
