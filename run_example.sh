set -e
set -x

PBC_HOME="$( cd "$(dirname "${BASH_SOURCE[0]}")/." && pwd )"
./install_pbc.sh
cd ${PBC_HOME}/example
./run_example.sh

set +x
set +e
