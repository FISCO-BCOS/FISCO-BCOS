set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(${BOOSTSSL_UTILITIES_TARGET} ${SRC_LIST} ${HEADERS})
# target_compile_options(${BOOSTSSL_UTILITIES_TARGET} PRIVATE -Wno-error -Wno-unused-variable)

find_package(Boost CONFIG QUIET REQUIRED log chrono system filesystem iostreams thread)
target_link_libraries(${BOOSTSSL_UTILITIES_TARGET} PUBLIC Boost::log Boost::chrono Boost::iostreams Boost::filesystem Boost::thread)

# install utilities target
install(
    TARGETS ${BOOSTSSL_UTILITIES_TARGET}
    EXPORT "${TARGETS_EXPORT_NAME}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)