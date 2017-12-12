# This scirpt builds universal OpenCL ICD Loader that searches for OpenCL
# implementations at run-time. The package also includes OpenCL headers.

include("${CMAKE_CURRENT_LIST_DIR}/helpers.cmake")

prepare_package_source(opencl-icd-loader 0.0 https://github.com/KhronosGroup/OpenCL-ICD-Loader/archive/master/OpenCL-ICD-Loader.tar.gz)

if (NOT EXISTS ${SOURCE_DIR}/inc/CL)
    download_and_unpack(https://github.com/KhronosGroup/OpenCL-Headers/archive/master/OpenCL-Headers.tar.gz ${SOURCE_DIR} STATUS)
    file(RENAME ${SOURCE_DIR}/OpenCL-Headers-master ${SOURCE_DIR}/inc/CL)
endif()

execute_process(COMMAND cmake ${SOURCE_DIR} -G "Visual Studio 14 2015 Win64"
                WORKING_DIRECTORY ${BUILD_DIR})

execute_process(COMMAND cmake --build ${BUILD_DIR}
                              --target OpenCL --config Release)

file(COPY ${BUILD_DIR}/Release/OpenCl.lib DESTINATION ${INSTALL_DIR}/win64/lib/)
file(COPY ${BUILD_DIR}/bin/Release/OpenCl.dll DESTINATION ${INSTALL_DIR}/win64/bin/)
file(COPY ${SOURCE_DIR}/inc/CL DESTINATION ${INSTALL_DIR}/win64/include/)

create_package(${PACKAGE_NAME} ${INSTALL_DIR})
