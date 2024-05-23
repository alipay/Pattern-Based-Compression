set -x
set -e

PBC_HOME="$( cd "$(dirname "${BASH_SOURCE[0]}")/." && pwd )"
PBC_INTEGRATION_TEST_DIR=${PBC_HOME}/testresult/integration_test

date_time=`date +"%Y%m%d%H%M"`
TRAIN_PATTERN_LOG=${PBC_INTEGRATION_TEST_DIR}/train_pattern.log.${date_time}
TEST_COMPRESS_LOG=${PBC_INTEGRATION_TEST_DIR}/test_compress.log.${date_time}
COMPRESS_LOG=${PBC_INTEGRATION_TEST_DIR}/compress.log.${date_time}
DECOMPRESS_LOG=${PBC_INTEGRATION_TEST_DIR}/decompress.log.${date_time}

mkdir -p ${PBC_INTEGRATION_TEST_DIR}

cp ${PBC_HOME}/dataset/Apache ${PBC_INTEGRATION_TEST_DIR}/test_data

echo "run pbc integration test"

# run train-pattern
${PBC_HOME}/bin/pbc --train-pattern -i ${PBC_INTEGRATION_TEST_DIR}/test_data -p ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_only --compress-method pbc_only --pattern-size 100 --train-data-number 2000 --train-thread-num 64 >> ${TRAIN_PATTERN_LOG} 2>&1
${PBC_HOME}/bin/pbc --train-pattern -i ${PBC_INTEGRATION_TEST_DIR}/test_data -p ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_fse --compress-method pbc_fse --pattern-size 100 --train-data-number 2000 --train-thread-num 64 >> ${TRAIN_PATTERN_LOG} 2>&1
${PBC_HOME}/bin/pbc --train-pattern -i ${PBC_INTEGRATION_TEST_DIR}/test_data -p ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_fsst --compress-method pbc_fsst --pattern-size 100 --train-data-number 2000 --train-thread-num 64 >> ${TRAIN_PATTERN_LOG} 2>&1
${PBC_HOME}/bin/pbc --train-pattern -i ${PBC_INTEGRATION_TEST_DIR}/test_data -p ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_zstd --compress-method pbc_zstd --pattern-size 100 --train-data-number 2000 --train-thread-num 64 >> ${TRAIN_PATTERN_LOG} 2>&1

# run test-compress
${PBC_HOME}/bin/pbc --test-compress -i ${PBC_INTEGRATION_TEST_DIR}/test_data -p ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_only --compress-method pbc_only >> ${TEST_COMPRESS_LOG} 2>&1
${PBC_HOME}/bin/pbc --test-compress -i ${PBC_INTEGRATION_TEST_DIR}/test_data -p ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_fse --compress-method pbc_fse >> ${TEST_COMPRESS_LOG} 2>&1
${PBC_HOME}/bin/pbc --test-compress -i ${PBC_INTEGRATION_TEST_DIR}/test_data -p ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_fsst --compress-method pbc_fsst >> ${TEST_COMPRESS_LOG} 2>&1
${PBC_HOME}/bin/pbc --test-compress -i ${PBC_INTEGRATION_TEST_DIR}/test_data -p ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_zstd --compress-method pbc_zstd >> ${TEST_COMPRESS_LOG} 2>&1

# run compress file and decompress file
${PBC_HOME}/bin/pbc -c -i ${PBC_INTEGRATION_TEST_DIR}/test_data -p ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_only -o ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_only_compress >> ${COMPRESS_LOG} 2>&1
${PBC_HOME}/bin/pbc -d -i ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_only_compress -p ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_only -o ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_only_decompress >> ${DECOMPRESS_LOG} 2>&1

cat ${PBC_INTEGRATION_TEST_DIR}/test_data | ${PBC_HOME}/bin/pbc -c -p ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_only | gzip -c | gzip -c -d | ${PBC_HOME}/bin/pbc -d -p ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_only -o ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_only_decompress2

# compare the original file with the file after compression and decompression.
cmp ${PBC_INTEGRATION_TEST_DIR}/test_data ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_only_decompress
cmp ${PBC_INTEGRATION_TEST_DIR}/test_data ${PBC_INTEGRATION_TEST_DIR}/test_data_2000_100_pbc_only_decompress2

set +e
set +x
