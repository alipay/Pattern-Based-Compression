cmake_minimum_required(VERSION 3.5.0)

project("PBC Third Party" C CXX)

set(CXX_STANDARD 14)
set(CXX_STANDARD_REQUIRED ON)

# Required CMake modules
include(ExternalProject)

# Get number of physical CPU cores and megabytes of available memory
cmake_host_system_information(RESULT num_cores QUERY NUMBER_OF_PHYSICAL_CORES)
cmake_host_system_information(RESULT available_memory_mb QUERY AVAILABLE_PHYSICAL_MEMORY)

execute_process(
    COMMAND ldd --version
    COMMAND head -1
    COMMAND cut -d ")" -f 2
    COMMAND cut -d " " -f 2
    OUTPUT_VARIABLE GLIBC_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Guess the number of building jobs based on the available memories
set(jobs_by_cpu ${num_cores})
math(EXPR jobs_by_mem "${available_memory_mb} / 1024 / 2")
if (jobs_by_mem EQUAL 0)
    set(jobs_by_mem 1)
endif()

# Set the number of building jobs to min(jobs_by_mem, jobs_by_cpu),
# if BUILDING_JOBS_NUM has not been set or set to 0.
if (NOT BUILDING_JOBS_NUM OR BUILDING_JOBS_NUM EQUAL 0)
    set(BUILDING_JOBS_NUM ${jobs_by_cpu})
    if (BUILDING_JOBS_NUM GREATER jobs_by_mem)
        set(BUILDING_JOBS_NUM ${jobs_by_mem})
    endif()
endif()

message(STATUS "Number of online physcial CPU cores: ${num_cores}")
message(STATUS "Available physical memory: ${available_memory_mb} MB")
message(STATUS "Building third party with ${BUILDING_JOBS_NUM} jobs")
message(STATUS "Glibc version: ${GLIBC_VERSION}")

if (NOT DOWNLOAD_DIR)
    set(DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}/downloads)
endif()
set(BUILD_INFO_DIR ${CMAKE_CURRENT_BINARY_DIR}/build-info)
set(BUILDING_PATH "${CMAKE_INSTALL_PREFIX}/bin:${CMAKE_INSTALL_PREFIX}/sbin:$ENV{PATH}")
set(ACLOCAL_PATH "${CMAKE_INSTALL_PREFIX}/share/aclocal:${CMAKE_INSTALL_PREFIX}/share/aclocal-1.15")

#set(extra_link_libs "-static-libstdc++ -static-libgcc")
if (${ENABLE_CXX11_ABI})
    set(extra_cpp_flags "-D_GLIBCXX_USE_CXX11_ABI=1")
else()
    set(extra_cpp_flags "-D_GLIBCXX_USE_CXX11_ABI=0")
endif()

if(GLIBC_VERSION VERSION_LESS 2.17)
    set(extra_link_libs "${extra_link_libs} -lrt")
endif()

set(common_cmake_args
    -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
    "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -fPIC ${extra_cpp_flags}"
    "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS} -fno-omit-frame-pointer -fPIC"
    "-DCMAKE_EXE_LINKER_FLAGS=${extra_link_libs}"
    "-DCMAKE_SHARED_LINKER_FLAGS=${extra_link_libs}"
    -DCMAKE_INCLUDE_PATH=${CMAKE_INSTALL_PREFIX}/include
    -DCMAKE_LIBRARY_PATH=${CMAKE_INSTALL_PREFIX}/lib
)

set(common_configure_args
    --prefix=${CMAKE_INSTALL_PREFIX}
)

file(MAKE_DIRECTORY ${CMAKE_INSTALL_PREFIX})

set(common_configure_envs
    "env"
    "CC=${CMAKE_C_COMPILER}"
    "CXX=${CMAKE_CXX_COMPILER}"
    "CFLAGS=${CMAKE_C_FLAGS} -fno-omit-frame-pointer -fPIC -O3 -D_DEFAULT_SOURCE -D_GNU_SOURCE ${extra_cpp_flags}"
    "CXXFLAGS=${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -fPIC -O3 -D_DEFAULT_SOURCE -D_GNU_SOURCE ${extra_cpp_flags}"
    "CPPFLAGS=-isystem ${CMAKE_INSTALL_PREFIX}/include ${extra_cpp_flags}"
    "LDFLAGS=-L${CMAKE_INSTALL_PREFIX}/lib64 ${extra_link_libs}"
    "PATH=${BUILDING_PATH}"
    "ACLOCAL_PATH=${ACLOCAL_PATH}"
)

set(ALL_TARGETS
    boost
    colm
    hyperscan
    ragel
    zstd
    gflags
    googletest
    cereal
)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../cmake)

foreach(target ${ALL_TARGETS})
    include(externals/${target}.cmake)
    list(APPEND CLEAN_TARGETS ${target}-clean)
endforeach()

macro(maybe_add_dependencies depender)
    if (TARGET ${depender})
        foreach (dependee ${ARGN})
            if (TARGET ${dependee})
                add_dependencies(${depender} ${dependee})
            endif()
        endforeach()
    endif()
endmacro()

maybe_add_dependencies(ragel colm)
maybe_add_dependencies(hyperscan boost ragel)

add_custom_target(
    clean-all
    DEPENDS ${CLEAN_TARGETS}
)

