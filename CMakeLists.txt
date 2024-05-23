cmake_minimum_required(VERSION 3.0.0)
project(pbc)
enable_testing ()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to Debug as none was specified.")
  set(CMAKE_BUILD_TYPE "Debug")
endif()

message( STATUS "Architecture: ${CMAKE_SYSTEM_PROCESSOR}" )

SET(src_dir ${CMAKE_SOURCE_DIR}/src)
SET(third_party_dir ${CMAKE_SOURCE_DIR}/third-party)

SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

INCLUDE_DIRECTORIES(
  SYSTEM ${third_party_dir}/install/include
  ${src_dir}
)

LINK_DIRECTORIES(
  ${third_party_dir}/install/lib
  ${third_party_dir}/install/lib64
)

SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g -ggdb -std=c++17 -Wno-deprecated -fPIC")
SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_USE_CXX11_ABI=0")
SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-unused-function -g -ggdb -fPIC")

option(ENABLE_ASAN "Whether to turn AddressSanitizer ON or OFF" OFF)
option(ENABLE_COVERAGE "Whether to turn unit test coverage ON or OFF" OFF)
option(ENABLE_WERROR "Whether to error on warnings" ON)
option(ENABLE_TSAN "Whether to turn Thread Sanitizer ON or OFF" OFF)
option(ENABLE_THIN_LTO "Whether to build with thin lto -flto=thin" OFF)

if (ENABLE_WERROR)
    add_compile_options(-Werror)
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -Wno-sign-compare -O3")
  SET(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -Wno-sign-compare -O3")
  SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -fuse-ld=lld")
  SET(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -fuse-ld=lld")
  SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS}")
  SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS}")
  SET(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fuse-ld=lld")
  SET(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fuse-ld=lld")
  SET(CMAKE_AR llvm-ar)
  SET(CMAKE_RANLIB llvm-ranlib)
else()
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-sign-compare -Wno-error=format=")
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-sign-compare -Wno-error=format=")
  SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3")
  SET(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -O3")
endif()

if (ENABLE_THIN_LTO)
  message(STATUS "Compiling in thinlto mode")
  SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto=thin")
  SET(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -flto=thin")
  SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto=thin")
  SET(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -flto=thin")
endif()

option(ENABLE_COVERAGE "Enable gcov (debug, Linux builds only)" OFF)
if (ENABLE_COVERAGE AND NOT WIN32 AND NOT APPLE)
  SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} --coverage -DCOVERAGE_TEST_USE_GCOV")
  SET(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} --coverage -DCOVERAGE_TEST_USE_GCOV")
ENDIF()

if (ENABLE_ASAN)
  if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0))
    message(STATUS "Compiling with AddressSanitizer and UndefinedBehaviorSanitizer")
    SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize-recover=address -fno-sanitize=vptr")
    SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize-recover=address -fno-sanitize=vptr")
    SET (CMAKE_LD_FLAGS "${CMAKE_LD_FLAGS} -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize-recover=address -fno-sanitize=vptr")
  else()
    message(STATUS "Compiling with AddressSanitizer")
    SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fsanitize=address -fno-omit-frame-pointer")
    SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fsanitize=address -fno-omit-frame-pointer")
    SET (CMAKE_LD_FLAGS "${CMAKE_LD_FLAGS} -g -fsanitize=address -fno-omit-frame-pointer")
  endif()
endif()

if (ENABLE_TSAN)
    if (ENABLE_ASAN)
        MESSAGE(FATAL_ERROR "ENABLE_TSAN cannot be combined with ENABLE_ASAN")
    endif()
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=thread")
    set(ENV{TSAN_OPTIONS} "report_atomic_races=0")
    add_compile_options(-fsanitize=thread)
    add_compile_options(-g)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
endif()

SET(DEPS_LIBRARIES
  libzstd.a
  libhs.a
)

SET(SYSTEM_LIBRARIES pthread)

SET(LIBRARIES ${DEPS_LIBRARIES} ${SYSTEM_LIBRARIES})

add_subdirectory(src)

