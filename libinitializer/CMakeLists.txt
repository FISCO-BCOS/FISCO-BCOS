file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

if (BUILD_GM)
    string(REPLACE "SecureInitializer.cpp" "" SRC_LIST "${SRC_LIST}")
else()
    string(REPLACE "SecureInitializerGm.cpp" "" SRC_LIST "${SRC_LIST}")
endif()

add_library(initializer ${SRC_LIST} ${HEADERS})

target_link_libraries(initializer PUBLIC ledger Boost::program_options)
target_link_libraries(initializer INTERFACE security rpc channelserver)

install(TARGETS initializer RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
