
set(SOURCES
	ExtVMFace.cpp
	VM.cpp
	VMOpt.cpp
	VMCalls.cpp
	VMFactory.cpp
	CoverTool.cpp
)

if (EVMJIT)
	list(APPEND SOURCES
		JitVM.cpp
		SmartVM.cpp
	)
endif()

file(GLOB ETHCALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ethcall/*.cpp")
file(GLOB HEADERS "*.h" "${CMAKE_CURRENT_SOURCE_DIR}/ethcall/*.h")
list(APPEND SOURCES ${ETHCALL_SOURCES})

#remove headers
set(REMOVE_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/ethcall/EthcallRingSig.h 
    ${CMAKE_CURRENT_SOURCE_DIR}/ethcall/EthcallGroupSig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ethcall/VerifyZkg.h) 
list(REMOVE_ITEM HEADERS ${REMOVE_HEADERS})

#remove specified sources
set(REMOVE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ethcall/VerifyZkg.cpp)
list(REMOVE_ITEM SOURCES ${REMOVE_SOURCES})

if (ZKG_VERIFY)
list(APPEND HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/ethcall/VerifyZkg.h)
list(APPEND SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ethcall/VerifyZkg.cpp)
add_compile_options(-DMULTICORE)
add_compile_options(-DBINARY_OUTPUT)
add_compile_options(-DCURVE_ALT_BN128)
add_compile_options(-DNO_PROCPS)
endif()

find_package(Eth)

add_library(evm ${SOURCES} ${HEADERS})
if(GROUPSIG)
    find_package(PBC REQUIRED)
	list(APPEND HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/ethcall/EthcallRingSig.h) 
    list(APPEND HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/ethcall/EthcallGroupSig.h)
	message(STATUS "new headers: ${HEADERS}")
    include_directories(${GROUP_SIG_INCLUDE_DIRS})
    add_dependencies(evm pbc)
    add_dependencies(evm pbc_sig)
    add_dependencies(evm group_sig)
    add_dependencies(evm group_sig_devcore)
endif()

#message(STATUS "**headers: ${HEADERS}")
#message(STATUS "**sources: ${SOURCES}")

target_link_libraries(evm ${Eth_ETHCORE_LIBRARIES})
target_link_libraries(evm ${Eth_EVMCORE_LIBRARIES})
target_link_libraries(evm paillier)

if(GROUPSIG)
    target_link_libraries(evm gmp)
    target_link_libraries(evm pbc)
    target_link_libraries(evm pbc_sig)
    target_link_libraries(evm group_sig)
    target_link_libraries(evm group_sig_devcore)
    target_link_libraries(evm Cryptopp)
endif()

if (ZKG_VERIFY)
include_directories(${LIBZKG_INCLUDE_DIR})
target_link_libraries(evm Zkg)
target_link_libraries(evm Zkg::circuit)
target_link_libraries(evm Zkg::snark)
target_link_libraries(evm Zkg::ff)
target_link_libraries(evm Zkg::zm)
target_link_libraries(evm gmp)
target_link_libraries(evm stdc++)
target_link_libraries(evm procps)
target_link_libraries(evm gmpxx)
target_link_libraries(evm gomp)

find_package(LevelDB)
target_include_directories(evm PRIVATE ${LEVELDB_INCLUDE_DIRS})
target_link_libraries(evm ${LEVELDB_LIBRARIES})
endif()

target_include_directories(evm PRIVATE .. ${JSONCPP_INCLUDE_DIR})

if (EVMJIT)
	target_include_directories(evm PRIVATE ../evmjit/include)
	target_link_libraries(evm ${Eth_EVMJIT_LIBRARIES})
endif()

install(TARGETS evm RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
