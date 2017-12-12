# This script downloads LLVM source and builds it with Visual Studio.

include("${CMAKE_CURRENT_LIST_DIR}/helpers.cmake")

function(build_llvm CONFIGURATION)
    if (CONFIGURATION STREQUAL "Release")
        set(ASSERTS OFF)
        set(CONFIG "Release")
    elseif (CONFIGURATION STREQUAL "Debug")
        set(ASSERTS ON)
        set(CONFIG "RelWithDebInfo")
    else()
        message(FATAL_ERROR "Wrong configuration")
    endif()
    set(INSTALL_DIR "${INSTALL_DIR}/win64/${CONFIGURATION}")

    execute_process(COMMAND cmake ${SOURCE_DIR} -G "Visual Studio 14 2015 Win64"
                            -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}
                            -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_BUILD_TOOLS=OFF
                            -DLLVM_ENABLE_ASSERTIONS=${ASSERTS}
                    WORKING_DIRECTORY ${BUILD_DIR})

    execute_process(COMMAND cmake --build ${BUILD_DIR}
                                  --target install --config ${CONFIG})
endfunction(build_llvm)

prepare_package_source(llvm 3.8.0 http://llvm.org/releases/3.8.0/llvm-3.8.0.src.tar.xz)

build_llvm("Release")
# build_llvm("Debug")

create_package(${PACKAGE_NAME} ${INSTALL_DIR})
