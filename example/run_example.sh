#!/usr/bin/bash
set -e
set -x

PBC_EXAMPLE_HOME="$( cd "$(dirname "${BASH_SOURCE[0]}")/." && pwd )"

mkdir -p ${PBC_EXAMPLE_HOME}/dataset
cp ../dataset/Apache ${PBC_EXAMPLE_HOME}/dataset/test_data

clang++ pbc_train_pattern.cc -L/usr/local/lib/pbc -I/usr/local/include -lpbc -lpbc_fse -lpbc_fsst -lzstd -lhs -lpthread -o pbc_train_pattern
clang++ pbc_compress.cc -L/usr/local/lib/pbc -I/usr/local/include -lpbc -lpbc_fse -lpbc_fsst -lzstd -lhs -lpthread -o pbc_compress
./pbc_train_pattern ${PBC_EXAMPLE_HOME}/dataset/test_data ${PBC_EXAMPLE_HOME}/dataset/test_data.pat
./pbc_compress ${PBC_EXAMPLE_HOME}/dataset/test_data ${PBC_EXAMPLE_HOME}/dataset/test_data.pat

set +x
set +e
