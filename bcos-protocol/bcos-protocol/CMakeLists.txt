file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

aux_source_directory(. SRC_LIST)
aux_source_directory(./amop SRC_LIST)

add_library(${PROTOCOL_TARGET} ${SRC_LIST} ${HEADERS})

target_link_libraries(${PROTOCOL_TARGET} PUBLIC ${CRYPTO_TARGET} ${CODEC_TARGET} bcos-framework)

include(GNUInstallDirs)
install(TARGETS ${PROTOCOL_TARGET} EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
# install(DIRECTORY "bcos-protocol" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")