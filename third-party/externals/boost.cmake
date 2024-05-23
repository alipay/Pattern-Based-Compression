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

set(name boost)
set(source_dir ${CMAKE_CURRENT_BINARY_DIR}/${name}/source)
get_filename_component(compiler_path ${CMAKE_CXX_COMPILER} DIRECTORY)
ExternalProject_Add(
    ${name}
    URL https://boostorg.jfrog.io/artifactory/main/release/1.67.0/source/boost_1_67_0.tar.gz
    URL_HASH MD5=4850fceb3f2222ee011d4f3ea304d2cb
    TIMEOUT 120
    DOWNLOAD_NAME boost_1_67_0.tar.gz
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/${name}
    TMP_DIR ${BUILD_INFO_DIR}
    STAMP_DIR ${BUILD_INFO_DIR}
    DOWNLOAD_DIR ${DOWNLOAD_DIR}
    SOURCE_DIR ${source_dir}
    CONFIGURE_COMMAND ""
    CONFIGURE_COMMAND
        ./bootstrap.sh
            --without-icu
            --without-libraries=python,test,stacktrace,mpi,log,graph,graph_parallel
            --prefix=${CMAKE_INSTALL_PREFIX}
    BUILD_COMMAND
        ./b2 install
            -d0
            -j${BUILDING_JOBS_NUM}
            --prefix=${CMAKE_INSTALL_PREFIX}
            --disable-icu
            include=${CMAKE_INSTALL_PREFIX}/include
            linkflags=-L${CMAKE_INSTALL_PREFIX}/lib
            "cxxflags=-fPIC ${extra_cpp_flags}"
            runtime-link=static
            link=static
            variant=release
    BUILD_IN_SOURCE 1
    INSTALL_COMMAND ""
    LOG_CONFIGURE TRUE
    LOG_BUILD TRUE
    LOG_INSTALL TRUE
    DOWNLOAD_NO_PROGRESS 1
)

ExternalProject_Add_Step(${name} setup-compiler
    DEPENDEES configure
    DEPENDERS build
    COMMAND
        echo "using gcc : : ${CMAKE_CXX_COMPILER} $<SEMICOLON>"
            > ${source_dir}/tools/build/src/user-config.jam
    WORKING_DIRECTORY ${source_dir}
)

ExternalProject_Add_Step(${name} clean
    EXCLUDE_FROM_MAIN TRUE
    ALWAYS TRUE
    DEPENDEES configure
    COMMAND ./b2 clean
    COMMAND rm -f ${BUILD_INFO_DIR}/${name}-build
    WORKING_DIRECTORY ${source_dir}
)

ExternalProject_Add_StepTargets(${name} clean)

