include("${CMAKE_CURRENT_LIST_DIR}/helpers.cmake")

prepare_package_source(boost 1.61 http://sourceforge.net/projects/boost/files/boost/1.61.0/boost_1_61_0.tar.gz)

if (NOT EXISTS ${SOURCE_DIR}/b2.exe)
    execute_process(COMMAND bootstrap.bat WORKING_DIRECTORY ${SOURCE_DIR})
endif()

execute_process(COMMAND ./b2 -j4
                stage --stagedir=${INSTALL_DIR}/win64 --build-dir=${BUILD_DIR}
                link=static
                runtime-link=shared
                variant=release
                threading=multi
                address-model=64
                --with-filesystem
                --with-system
                --with-thread
                --with-date_time
                --with-regex
                --with-test
                --with-program_options
                --with-random
                --with-atomic
                WORKING_DIRECTORY ${SOURCE_DIR})

message("Copying boost headers")
file(COPY ${SOURCE_DIR}/boost DESTINATION ${INSTALL_DIR}/win64/include)

create_package(${PACKAGE_NAME} ${INSTALL_DIR})
