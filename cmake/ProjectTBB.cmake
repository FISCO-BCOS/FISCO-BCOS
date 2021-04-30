include(ExternalProject)

set(TBB_LIB_SUFFIX a)
if (APPLE)
    set(ENABLE_STD_LIB stdlib=libc++)
    set(TBB_LIB_SUFFIX dylib)
endif()

if (APPLE)
    set(SED_CMMAND sed -i .bkp)
    set(COMPILER_FLAGS "-Wno-defaulted-function-deleted -Wno-shadow")
else()
    set(SED_CMMAND sed -i)
    set(COMPILER_FLAGS "")
endif()

if(APPLE AND ${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "arm64")
    set(FIXED_MARCH_TYPE armv8)
else()
    set(FIXED_MARCH_TYPE ${MARCH_TYPE})
endif()


set(TBB_SRC_FILE_URL  file://${THIRD_PARTY_ROOT}/oneTBB-2020.3.tar.gz)
set(TBB_FILE_DIGEST SHA256=ebc4f6aa47972daed1f7bf71d100ae5bf6931c2e3144cf299c8cc7d041dca2f3)


ExternalProject_Add(tbb
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NO_PROGRESS 1
    DOWNLOAD_NAME oneTBB-2020.1.tar.gz
    # DOWNLOAD_NAME oneTBB-2020.3.tar.gz
    # URL https://codeload.github.com/oneapi-src/oneTBB/tar.gz/v2020.3
        # https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/libs/oneTBB-2020.3.tar.gz
    # URL_HASH SHA256=ebc4f6aa47972daed1f7bf71d100ae5bf6931c2e3144cf299c8cc7d041dca2f3
    URL ${TBB_SRC_FILE_URL}
    URL_HASH ${TBB_FILE_DIGEST}
    BUILD_IN_SOURCE 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    LOG_MERGED_STDOUTERR 1
    CONFIGURE_COMMAND ""
    # BUILD_COMMAND make extra_inc=big_iron.inc ${ENABLE_STD_LIB}
    PATCH_COMMAND ${SED_CMMAND} -e "39s/^//p" ./build/macos.inc
    PATCH_COMMAND && ${SED_CMMAND} -e "39s#^.*#ifeq\ \(\$\(shell\ /usr/sbin/sysctl\ -n\ hw.machine\),arm64\)\\nexport\ arch:=arm64\\nelse#g" ./build/macos.inc
    PATCH_COMMAND && ${SED_CMMAND} -e "49s/^//p" ./build/macos.inc
    PATCH_COMMAND && ${SED_CMMAND} -e "49s#^.*#endif#" ./build/macos.inc
    BUILD_COMMAND make -j2 ${ENABLE_STD_LIB} tbb
    INSTALL_COMMAND bash -c "/bin/cp -f ./build/*_release/libtbb.${TBB_LIB_SUFFIX}* ${CMAKE_SOURCE_DIR}/deps/lib/"
    BUILD_BYPRODUCTS ${CMAKE_SOURCE_DIR}/deps/lib/libtbb.${TBB_LIB_SUFFIX}
)

ExternalProject_Get_Property(tbb SOURCE_DIR)
add_library(TBB STATIC IMPORTED)
set(TBB_INCLUDE_DIR ${SOURCE_DIR}/include)
set(TBB_LIBRARY ${CMAKE_SOURCE_DIR}/deps/lib/libtbb.${TBB_LIB_SUFFIX})
file(MAKE_DIRECTORY ${TBB_INCLUDE_DIR})  # Must exist.
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/lib/)  # Must exist.

set_property(TARGET TBB PROPERTY IMPORTED_LOCATION ${TBB_LIBRARY})
set_property(TARGET TBB PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${TBB_INCLUDE_DIR})
add_dependencies(TBB tbb)
unset(SOURCE_DIR)
