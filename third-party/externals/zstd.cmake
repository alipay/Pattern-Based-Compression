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

set(name zstd)
set(source_dir ${CMAKE_CURRENT_BINARY_DIR}/${name}/source)
# build with ZSTD_MULTITHREAD to improve zstd compress rate
set (zstd_envs "env" "CFLAGS=-fPIC -DZSTD_MULTITHREAD")
ExternalProject_Add(
    ${name}
    URL https://github.com/facebook/zstd/releases/download/v1.4.3/zstd-1.4.3.tar.gz
    URL_HASH MD5=8581c03b2f56c14ff097a737e60847b3
    TIMEOUT 120
    DOWNLOAD_NAME zstd-1.4.3.tar.gz
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/${name}
    TMP_DIR ${BUILD_INFO_DIR}
    STAMP_DIR ${BUILD_INFO_DIR}
    DOWNLOAD_DIR ${DOWNLOAD_DIR}
    SOURCE_DIR ${source_dir}
    CONFIGURE_COMMAND ${zstd_envs}
    BUILD_COMMAND
        make -e -s -j${BUILDING_JOBS_NUM}
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

