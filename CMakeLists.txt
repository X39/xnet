cmake_minimum_required(VERSION 2.8.12)
project(xnet)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MACOSX_RPATH 1)

# Enable multithreaded compilation in MSVC
if (MSVC)
  add_definitions(/MP)
  add_definitions(/wd4100)
endif()

set(ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/output)
set(LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/output)
set(RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/output)

#####################
##                 ##
##  FILES SECTION  ##
##                 ##
#####################

file(GLOB xnet_src "${PROJECT_SOURCE_DIR}/src/*.h" "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.c")

############################
##                        ##
##  DEPENDENCIES SECTION  ##
##                        ##
############################

add_subdirectory("${PROJECT_SOURCE_DIR}/XCLib/")

find_package(Threads)


option(XCLIB_BUILD_EXECUTABLE "BUILD EXECUTABLE" ON)


if (XCLIB_BUILD_EXECUTABLE)
    add_executable(xnet ${xnet_src})
    target_link_libraries(xnet ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT} ${ST_CXXFS_LIBS})
    target_include_directories(xnet PUBLIC ${PROJECT_SOURCE_DIR}/src)
    
    target_compile_options(xnet PRIVATE
     $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
          -Wall -Wno-unknown-pragmas>
     $<$<CXX_COMPILER_ID:MSVC>:
          /W4>)
endif ()