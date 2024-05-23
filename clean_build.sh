PBC_HOME="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo "clean all artifactories"

set -x
rm -rf \
    ${PBC_HOME}/build \
    ${PBC_HOME}/build_asan \
    ${PBC_HOME}/build_cov \
    ${PBC_HOME}/bin \
    ${PBC_HOME}/third-party/build
pushd ${PBC_HOME}/third-party
git clean -xdf
popd
pushd ${PBC_HOME}/third-party
git clean -xdf
popd
set +x
