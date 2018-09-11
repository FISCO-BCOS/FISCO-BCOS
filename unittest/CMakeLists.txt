if (TESTS)
	aux_source_directory(. SRC_LIST)

	macro (add_sources)
		foreach (_src ${ARGN})
			file (RELATIVE_PATH _relPath "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_LIST_DIR}")
			list (APPEND SRC "./${_relPath}/${_src}")
			set(SRC ${SRC} PARENT_SCOPE)
		endforeach()
	endmacro()

	add_subdirectory(libstorage)
	add_subdirectory(libprecompiled)
	add_subdirectory(libchannelserver)

	set(SRC_LIST ${SRC_LIST} ${SRC})
	enable_testing()

	file(GLOB HEADERS "*.h")
	add_executable(testeth ${SRC_LIST} ${HEADERS})

	find_package(Eth)
	find_package(Web3)

	target_include_directories(testeth PRIVATE ..)

	target_link_libraries(testeth ${Eth_EVM_LIBRARIES})
	target_link_libraries(testeth ${Web3_WEB3JSONRPC_LIBRARIES})
	target_link_libraries(testeth ${Web3_WEBTHREE_LIBRARIES})
	target_link_libraries(testeth JsonRpcCpp::Client)
	target_link_libraries(testeth pbftseal)
	target_link_libraries(testeth channelserver)
	target_link_libraries(testeth web3jsonrpc)
	target_link_libraries(testeth precompiled)
	target_link_libraries(testeth storage)

	if(BUILD_BOOST)
		target_include_directories(testeth PUBLIC ${BOOST_INCLUDE_DIR})
		target_link_libraries(testeth Boost::UnitTestFramework)
	else()
		find_package(Boost COMPONENTS system filesystem random thread regex unit_test_framework REQUIRED)
		target_link_libraries(testeth ${Boost_LIBRARIES})
	endif()

	target_link_libraries(testeth pthread)

	find_program(LCOV_TOOL lcov)
	message(STATUS "lcov tool: ${LCOV_TOOL}")
	add_test(unittest testeth)
	if (LCOV_TOOL)
		add_custom_target(coverage
			COMMAND ${LCOV_TOOL} -q -o ${CMAKE_BINARY_DIR}/coverage.info -c -d ${CMAKE_BINARY_DIR}
			COMMAND ${LCOV_TOOL} -q -o ${CMAKE_BINARY_DIR}/coverage.info -r ${CMAKE_BINARY_DIR}/coverage.info '/usr*' '${CMAKE_BINARY_DIR}/deps/*' '${CMAKE_SOURCE_DIR}/deps/*'
			COMMAND genhtml -q -o ${CMAKE_BINARY_DIR}/CodeCoverage ${CMAKE_BINARY_DIR}/coverage.info)
	endif (LCOV_TOOL)
endif(TESTS)

