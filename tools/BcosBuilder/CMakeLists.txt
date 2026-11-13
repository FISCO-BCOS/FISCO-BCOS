if(TESTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME BcosBuilderNodeConfigRegression
        COMMAND ${CMAKE_COMMAND} -E env
        "PYTHONDONTWRITEBYTECODE=1"
        ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/test -v)
    set_tests_properties(BcosBuilderNodeConfigRegression PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()
