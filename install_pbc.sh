set -e
set -x

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

HEADER_PATH="/usr/local/include/pbc"
LIB_PATH="/usr/local/lib/pbc"

if [ ! -z "$USE_ASAN" ]; then
    BUILD_DIR="build_asan"
elif [ ! -z "$USE_TSAN" ]; then
    BUILD_DIR="build_tsan"
elif [ ! -z "$COVERAGE" ]; then
    BUILD_DIR="build_cov"
else
    BUILD_DIR="build"
fi

mkdir -p $HEADER_PATH
mkdir -p $LIB_PATH
install -D src/compress-c.h $HEADER_PATH
install -D ${BUILD_DIR}/src/libpbc.a  ${BUILD_DIR}/src/deps/fse/libpbc_fse.a  ${BUILD_DIR}/src/deps/fsst/libpbc_fsst.a $LIB_PATH
install -D third-party/install/lib/libzstd.a third-party/install/lib64/libhs.a $LIB_PATH

set +x
set +e
