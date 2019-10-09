cmake_minimum_required(VERSION 2.8)

project(coros)

if(UNIX)
    enable_language(CXX ASM)
else()
    enable_language(CXX ASM-ATT)
endif()

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

aux_source_directory(${PROJECT_SOURCE_DIR}/deps/malog/src malog_SRC)
add_library(malog STATIC ${malog_SRC})
target_include_directories(malog PUBLIC ${PROJECT_SOURCE_DIR}/deps/malog/src)

include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/deps/malog/src)
aux_source_directory(${PROJECT_SOURCE_DIR}/src COROS_SRC)
if(UNIX)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(CPU_ARCH "x86_64")
    else()
        set(CPU_ARCH "i386")
    endif()
    set(ASM_EXT "sysv_elf_gas.S")
    SET(DEPENDENT_LIBRARIES uv pthread)
else()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(CPU_ARCH "x86_64")
    else()
        set(CPU_ARCH "i386")
    endif()
    set(ASM_EXT "ms_pe_gas.asm")
    SET(DEPENDENT_LIBRARIES uv ws2_32)
endif()

set(ASM_SOURCES "asm/make_${CPU_ARCH}_${ASM_EXT}"
                "asm/jump_${CPU_ARCH}_${ASM_EXT}"
                "asm/ontop_${CPU_ARCH}_${ASM_EXT}")

add_library(coros STATIC ${COROS_SRC} ${ASM_SOURCES})
target_include_directories(coros PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_subdirectory(examples)
add_subdirectory(rtmpd)
