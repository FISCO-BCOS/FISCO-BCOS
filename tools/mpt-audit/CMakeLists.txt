# mpt-audit: offline auditor and rollback tool for the path-addressed MPT.
# Same shape as tools/storage-tool — one .cpp, one executable named after it — but a much
# smaller link surface: the audits are header-only templates in bcos-ledger, so the tool needs
# only the state storage they scan and the ledger library those headers live in.
file(GLOB SRC_LIST "*.cpp")

find_package(Boost REQUIRED program_options)

foreach(source ${SRC_LIST})
    get_filename_component(filename ${source} NAME)
    string(REPLACE ".cpp" "" target_name ${filename})
    add_executable(${target_name} ${source})
    target_link_libraries(${target_name} storage ledger Boost::program_options)
    target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR})
endforeach()
