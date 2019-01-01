file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

if (BUILD_GM)
    string(REPLACE "SecureInitializer.cpp" "" SRC_LIST "${SRC_LIST}")
else()
    string(REPLACE "SecureInitializerGm.cpp" "" SRC_LIST "${SRC_LIST}")
endif()

add_library(initializer ${SRC_LIST} ${HEADERS})
target_include_directories(initializer SYSTEM BEFORE PUBLIC ${BOOST_INCLUDE_DIR})

target_link_libraries(initializer PUBLIC ledger channelserver rpc devcore dl)
install(TARGETS initializer RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
