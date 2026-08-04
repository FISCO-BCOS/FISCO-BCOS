vcpkg_extract_source_archive_ex(
    OUT_SOURCE_PATH MASTER_COPY_SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
    REF ${OPENSSL_VERSION}
)

# clang rejects integer-to-pointer assignment in TASSL s3_lib.c.
vcpkg_replace_string("${MASTER_COPY_SOURCE_PATH}/ssl/s3_lib.c"
    "        pctx->app_data = 1;"
    "        pctx->app_data = (void*)1;"
)
vcpkg_replace_string("${MASTER_COPY_SOURCE_PATH}/ssl/s3_lib.c"
    "        pctx->app_data = 0;"
    "        pctx->app_data = (void*)0;"
)

if(CMAKE_HOST_WIN32)
    vcpkg_acquire_msys(MSYS_ROOT PACKAGES make perl)
    set(MAKE "${MSYS_ROOT}/usr/bin/make.exe")
    set(PERL "${MSYS_ROOT}/usr/bin/perl.exe")
else()
    find_program(MAKE make)
    if(NOT MAKE)
        message(FATAL_ERROR "Could not find make. Please install it through your package manager.")
    endif()
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}"
    OPTIONS
        -DSOURCE_PATH=${MASTER_COPY_SOURCE_PATH}
        -DPERL=${PERL}
        -DMAKE=${MAKE}
        -DVCPKG_CONCURRENCY=${VCPKG_CONCURRENCY}
)

vcpkg_cmake_install()
vcpkg_fixup_pkgconfig()

file(GLOB HEADERS "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/*/include/openssl/*.h")
set(RESOLVED_HEADERS)
foreach(HEADER ${HEADERS})
    get_filename_component(X "${HEADER}" REALPATH)
    list(APPEND RESOLVED_HEADERS "${X}")
endforeach()

file(INSTALL ${RESOLVED_HEADERS} DESTINATION "${CURRENT_PACKAGES_DIR}/include/openssl")
file(INSTALL "${MASTER_COPY_SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
