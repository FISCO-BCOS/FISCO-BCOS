file(GLOB SRC_LIST "*.cpp")

find_package(Boost CONFIG QUIET REQUIRED program_options)

foreach(source ${SRC_LIST})
    get_filename_component(filename ${source} NAME)
    string(REPLACE ".cpp" "" target_name ${filename})
    add_executable(${target_name} ${source})
    target_link_libraries(${target_name} ${STORAGE_TARGET} framework Boost::program_options)
    target_include_directories(${target_name} PUBLIC ../bcos-storage)
endforeach()
