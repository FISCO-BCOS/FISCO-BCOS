file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

#aux_source_directory(. SRC_LIST)
#aux_source_directory(./amop SRC_LIST)

find_package(Threads REQUIRED)

include(GNUInstallDirs)
install(DIRECTORY "bcos-protocol" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")