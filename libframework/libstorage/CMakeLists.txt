add_library(${STORAGE_TARGET} StateStorage.cpp Table.cpp StorageInterface.cpp)
find_package(TBB CONFIG QUIET REQUIRED)
find_package(Boost CONFIG QUIET REQUIRED log chrono system filesystem iostreams thread)
target_link_libraries(${STORAGE_TARGET} PUBLIC Boost::log Boost::chrono Boost::iostreams Boost::filesystem Boost::thread TBB::tbb)
