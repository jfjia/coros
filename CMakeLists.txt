cmake_minimum_required(VERSION 2.8)

project(coros)

option(WITH_EXAMPLES "build examples" OFF)

enable_language(CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE DEBUG)
endif()

message("  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message("  CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")
message("  CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")

set(CMAKE_CXX_FLAGS "-std=c++11 -fPIC -fno-strict-aliasing -Wall ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -ggdb -D_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -ggdb -DNDEBUG")

message("  CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}: ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}")

include_directories(${PROJECT_SOURCE_DIR}/include)
aux_source_directory(${PROJECT_SOURCE_DIR}/src COROS_SRC)
if(UNIX)
    SET(DEPENDENT_LIBRARIES uv pthread)
else()
    SET(DEPENDENT_LIBRARIES uv ws2_32)
endif()

add_library(coros STATIC ${COROS_SRC})
target_include_directories(coros PRIVATE ${PROJECT_SOURCE_DIR}/src)

if(WITH_EXAMPLES)
    add_subdirectory(${PROJECT_SOURCE_DIR}/deps/malog)
    add_subdirectory(examples)
endif()
