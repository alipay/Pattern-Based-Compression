# Copyright 2023 The PBC Authors

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(name hyperscan)
set(source_dir ${CMAKE_CURRENT_BINARY_DIR}/${name}/source)
if (CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64)|(AARCH64)")
    set(url "https://github.com/kunpengcompute/hyperscan/archive/refs/tags/v5.3.0.aarch64.tar.gz")
    set(url_hash "ef337257bde6583242a739fab6fb161f")
    set(package_name hyperscan_aarch64.tar.gz)

else()
    set(url "https://github.com/intel/hyperscan/archive/refs/tags/v5.4.0.tar.gz")
    set(url_hash "65e08385038c24470a248f6ff2fa379b")
    set(package_name hyperscan_x86_64.tar.gz)
endif()

ExternalProject_Add(
    ${name}
    URL ${url}
    URL_HASH MD5=${url_hash}
    TIMEOUT 120
    DOWNLOAD_NAME ${package_name}
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/${name}
    TMP_DIR ${BUILD_INFO_DIR}
    STAMP_DIR ${BUILD_INFO_DIR}
    DOWNLOAD_DIR ${DOWNLOAD_DIR}
    SOURCE_DIR ${source_dir}
    CMAKE_ARGS
        ${common_cmake_args}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_COMPILER=/usr/bin/gcc
        -DCMAKE_CXX_COMPILER=/usr/bin/g++
    BUILD_IN_SOURCE 1
    INSTALL_COMMAND make -s install -j${BUILDING_JOBS_NUM} PREFIX=${CMAKE_INSTALL_PREFIX}
    LOG_CONFIGURE TRUE
    LOG_BUILD TRUE
    LOG_INSTALL TRUE
    DOWNLOAD_NO_PROGRESS 1
)

ExternalProject_Add_Step(${name} clean
    EXCLUDE_FROM_MAIN TRUE
    ALWAYS TRUE
    DEPENDEES configure
    COMMAND make clean -j
    COMMAND rm -f ${BUILD_INFO_DIR}/${name}-build
    WORKING_DIRECTORY ${source_dir}
)

ExternalProject_Add_StepTargets(${name} clean)

