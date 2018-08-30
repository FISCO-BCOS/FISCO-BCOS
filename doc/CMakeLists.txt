# Building docs script
# Requirements:
#   sudo apt-get install doxygen graphviz
#   [Build doxygen from CMake script](https://stackoverflow.com/questions/34878276/build-doxygen-from-cmake-script)  

if(NOT BUILD_DOCS)
  return()
endif()

find_package(Doxygen QUIET)

if(DOXYGEN_FOUND)
  # additional config
  set(doxyfile_in ${CMAKE_CURRENT_SOURCE_DIR}/.Doxyfile.in)
  set(doxyfile ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
  configure_file(${doxyfile_in} ${doxyfile} @ONLY)

# Adding docs target
add_custom_target(doc COMMAND ${DOXYGEN_EXECUTABLE} ${doxyfile}
                       WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                       COMMENT "Generating documentation with Doxygen..." VERBATIM)
elseif()
  message(WARNING "Doxygen is needed to build the documentation. Please install doxygen and graphviz") 
endif()