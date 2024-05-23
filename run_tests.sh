#!/usr/bin/bash
set -e
set -x

PBC_HOME="$( cd "$(dirname "${BASH_SOURCE[0]}")/." && pwd )"

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
    --gcov)
    COVERAGE=1
    shift
    ;;
    --pbc-integration-test)
    PBC_INTEGRATION_TEST=1
    shift
    ;;
    *)    # unknown option    POSITIONAL+=("$1") # save it in an array for later
    shift # past argument
    ;;
esac
done
set -- "${POSITIONAL[@]}" # restore positional parameters
##########################################

if [ ! -z "$USE_ASAN" ]; then
    echo "Unit testing with AddressSanitizer"
    BUILD_DIR="build_asan"
elif [ ! -z "$USE_TSAN" ]; then
    echo "Unit testing with ThreadSanitizer"
    BUILD_DIR="build_tsan"
elif [ ! -z "$COVERAGE" ]; then
    echo "Unit testing with coverage"
    BUILD_DIR="build_cov"
else
    BUILD_DIR="build"
fi

if [ -z "$GTEST_LOG_OUTPUT_DIR" ]; then
    export GTEST_LOG_OUTPUT_DIR=${PBC_HOME}/testresult
    mkdir -p $GTEST_LOG_OUTPUT_DIR
fi

if [ -z "$GTEST_OUTPUT" ]; then
    export GTEST_OUTPUT="xml:${PBC_HOME}/testresult/gtest/"
    mkdir -p ${PBC_HOME}/testresult/gtest/
fi

cp ${PBC_HOME}/dataset/Apache ${PBC_HOME}/dataset/test_data
echo "unit testing..."
pushd ${PBC_HOME}/${BUILD_DIR}

# set the timeout for unit tests 40 minutes
UNIT_TEST_TIMEOUT="2400"

timeout $UNIT_TEST_TIMEOUT ctest $TEST_ARGS CTEST_OUTPUT_ON_FAILURE -j 4 | tee $GTEST_LOG_OUTPUT_DIR/unit_tests_summary.log

[[ ! -z "$GTEST_LOG_OUTPUT_DIR" ]] && cp Testing/Temporary/LastTest.log $GTEST_LOG_OUTPUT_DIR/unit_tests_details.log || true

if grep -q "0 tests failed out" $GTEST_LOG_OUTPUT_DIR/unit_tests_summary.log; then
    echo "unit tests passed"
else
    echo "unit tests failed"
    exit 1
fi
popd

if [[ ! -z "$PBC_INTEGRATION_TEST" ]]; then
    echo "run pbc integration test"
    bash ${PBC_HOME}/integration_test.sh
    if [ $? -ne 0 ]; then
        echo "integration test failed"
        exit 1
    fi
fi
