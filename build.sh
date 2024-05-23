#!/usr/bin/bash
#require cmake version 3.0.0
PBC_HOME="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
set -e
set -x

if [ -z "$ACI_JOB_NAME" ]; then
    renice -n 10 $$
fi

BUILD_TYPE=Debug
THIRD_PARTY_BUILD_TYPE=Release
ENABLE_THIN_LTO=OFF

# Default compiler is Clang
USE_CLANG=1

######### parsing arguments #############
POSITIONAL=()
while [[ $# -gt 0 ]]
do
key="$1"

case $key in
    -a|--asan)
    USE_ASAN=1
    shift # past argument
    ;;
    -t|--tsan)
    USE_TSAN=1
    shift # past argument
    ;;
    -d|--debug)
    BUILD_TYPE=Debug
    shift # past argument
    ;;
    -r|--release)
    BUILD_TYPE=Release
    shift # past argument
    ;;
    --clang)
    USE_CLANG=1
    USE_GCC=""
    shift # past argument
    ;;
    --gcc)
    USE_CLANG=""
    USE_GCC=1
    shift # past argument
    ;;
    --gcov)
    USE_GCOV=1
    shift # past argument
    ;;
    --thin-lto)
    ENABLE_THIN_LTO=ON
    shift
    ;;
    -v|--verbose)
    MAKE_VERBOSE="VERBOSE=1"
    shift # past argument
    ;;
    *)    # unknown option    POSITIONAL+=("$1") # save it in an array for later
    shift # past argument
    ;;
esac
done
set -- "${POSITIONAL[@]}" # restore positional parameters

if [ -z "$PBC_MAKE_JOBS" ]; then
    PBC_MAKE_JOBS=`grep processor /proc/cpuinfo | wc -l`
fi
echo "PBC Project build with parallel $PBC_MAKE_JOBS"

if [ -z "$ACI_JOB_NAME" ]; then
    # in non-ACI environment, use the current timesteamp for version
    BUILD_TIMESTAMP=`date +"%Y%m%d%H%M"`
else
    # in ACI enviroment, use last commit time for version
    BUILD_TIMESTAMP=`git log -1 --format=%ct | xargs -I{} date -d @{} +%Y%m%d%H%M`
fi

CMAKE_FLAGS=""
CMAKE_CXX_FLAGS=""
LAUNCHER=""

if [ ! -z "$USE_ASAN" ]; then
    echo "Build with AddressSanitizer"
    CMAKE_FLAGS="$CMAKE_FLAGS -DENABLE_ASAN=ON"
    BUILD_FLAG=".asan"
    BUILD_DIR="build_asan"
elif [ ! -z "$USE_TSAN" ]; then
    echo "Build with ThreadSanitizer"
    CMAKE_FLAGS="$CMAKE_FLAGS -DENABLE_TSAN=ON"
    BUILD_FLAG=".tsan"
    BUILD_DIR="build_tsan"
elif [ ! -z "$USE_GCOV" ]; then
    echo "Build with coverage"
    CMAKE_FLAGS="$CMAKE_FLAGS -DENABLE_COVERAGE=ON"
    BUILD_FLAG=".cov"
    BUILD_DIR="build_cov"
else
    CMAKE_FLAGS="$CMAKE_FLAGS -DENABLE_ASAN=OFF"
    BUILD_DIR="build"
fi

if [ ! -z "$USE_CLANG" ]; then
    echo "Build with Clang"
    export CC=clang
    export CXX=clang++
fi

if [ ! -z "$USE_GCC" ]; then
    echo "Build with GCC"
    export CC=gcc
    export CXX=g++
fi

# build third-party with GCC
if [ ! -f "${GCC_HOME}/bin/gcc" ]; then
    GCC_HOME="/usr"
fi

mkdir -p ${PBC_HOME}/third-party/build
pushd ${PBC_HOME}/third-party/build
cmake \
    -DDOWNLOAD_DIR=${PBC_HOME}/third-party/downloads \
    -DCMAKE_INSTALL_PREFIX=${PBC_HOME}/third-party/install \
    -DCMAKE_CXX_COMPILER_LAUNCHER=$LAUNCHER \
${PBC_HOME}/third-party
make -j$PBC_MAKE_JOBS
popd

if [ ! -z "$USE_CHECKER" ]; then
    export CCC_CC=clang
    export CCC_CXX=clang++
    export CC=clang
    export CXX=clang++
    export REPORTS_DIR=${PBC_HOME}/testresult/analyzer-report/$BUILD_VERSION
    export MAKE_PREFIX="scan-build -v --use-c++=$CCC_CXX --use-cc=$CCC_CC --exclude ${PBC_HOME}/third-party/ --exclude ${PBC_HOME}/src/deps/ -o $REPORTS_DIR "
    export USE_CHECKER
    mkdir -p $REPORTS_DIR
    echo "ouptut static analyzer at $REPORTS_DIR"
fi

echo "building pbc..."
mkdir -p ${PBC_HOME}/${BUILD_DIR}
pushd ${PBC_HOME}/${BUILD_DIR}
$MAKE_PREFIX cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE $CMAKE_FLAGS \
   -DCMAKE_C_FLAGS="$CMAKE_C_FLAGS" \
   -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS"\
   -DCMAKE_CXX_COMPILER_LAUNCHER=$LAUNCHER \
   -DENABLE_THIN_LTO=$ENABLE_THIN_LTO \
   ..
$MAKE_PREFIX make -j$PBC_MAKE_JOBS $MAKE_VERBOSE
