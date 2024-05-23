/*
 * Copyright 2023 The PBC Authors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <vector>

#include "common/utils.h"
#include "compress/compress_factory.h"
#include "train/pbc_train.h"

DEFINE_string(dataset_path, "./", "dataset_path");

constexpr int MAX_PATTERN_SIZE = (1024 * 1024 * 8);
constexpr int DEFAULT_PATTERN_SIZE = 50;
constexpr int DEFAULT_DATASET_SIZE = 10000;
constexpr int SAMPLE_STEP = 100;
constexpr int MIN_RANDOM_DATA_LEN = 30;
constexpr int MAX_RANDOM_DATA_LEN = 50;
constexpr int MIN_PATTERN_LEN = 25;
constexpr int MAX_PATTERN_LEN = 30;
constexpr int MAX_RECORD_SIZE = 1024 * 1024;

const std::vector<std::string> test_datasets = {"./test_data"};
const std::vector<PBC::CompressMethod> compress_methods = {
    PBC::CompressMethod::PBC_ONLY, PBC::CompressMethod::PBC_FSE, PBC::CompressMethod::PBC_FSST,
    PBC::CompressMethod::PBC_ZSTD};
const std::vector<int> train_thread_nums = {0, 1, 16};

using PBC::INFO;

// Test random data with pattern
static void TestRandomDataWithPattern(bool contain_null_character) {
    int64_t data_size = DEFAULT_DATASET_SIZE * (MAX_RANDOM_DATA_LEN + MAX_PATTERN_LEN + 1);
    char* file_buffer = new char[data_size];

    char* pattern_buffer = new char[MAX_PATTERN_SIZE];

    // generate random dataset
    int64_t file_buffer_len = PBC::GenerateRandomDataWithPattern(
        file_buffer, data_size, DEFAULT_DATASET_SIZE, MIN_RANDOM_DATA_LEN, MAX_RANDOM_DATA_LEN,
        DEFAULT_PATTERN_SIZE, MIN_PATTERN_LEN, MAX_PATTERN_LEN, contain_null_character);
    EXPECT_GT(file_buffer_len, 0);
    std::string raw_data(file_buffer, file_buffer_len);
    std::vector<std::string> raw_data_vec = PBC::SplitString(raw_data, "\n");
    EXPECT_GT(raw_data_vec.size(), 0);
    for (PBC::CompressMethod compress_method : compress_methods) {
        for (int train_thread_num : train_thread_nums) {
            // pattern training

            char* records_buffer = nullptr;
            char* train_buffer = nullptr;
            char* pattern_buffer = nullptr;
            int64_t records_buffer_len = 0;
            int64_t train_buffer_len = 0;
            int64_t pattern_buffer_len = 0;
            int64_t record_num = 0;
            int32_t max_record_len = 0;

            records_buffer_len =
                PBC::ReadDataFromBuffer(TYPE_RECORD, file_buffer, file_buffer_len, &records_buffer,
                                        record_num, max_record_len);
            train_buffer_len = PBC::SamplingFromData(records_buffer, records_buffer_len, record_num,
                                                     &train_buffer, record_num / SAMPLE_STEP);
            EXPECT_EQ(raw_data_vec.size(), record_num);
            PBC::PBC_Train* pbc_train = new PBC::PBC_Train(compress_method, train_thread_num);
            pbc_train->PBC::PBC_Train::LoadData(train_buffer, train_buffer_len,
                                                /*data_type=*/TYPE_VARCHAR);
            pattern_buffer_len =
                pbc_train->PBC::PBC_Train::TrainPattern(DEFAULT_PATTERN_SIZE, &pattern_buffer);
            EXPECT_GT(pattern_buffer_len, 0);
            EXPECT_GT(strlen(pattern_buffer), 0);

            // compression test
            PBC::PBC_Compress* pbc_compress =
                PBC::CompressFactory::CreatePBCCompress(compress_method);
            EXPECT_TRUE(
                pbc_compress->PBC::PBC_Compress::ReadData(pattern_buffer, pattern_buffer_len))
                << "read pattern failed" << std::endl;
            // test ReadData
            EXPECT_EQ(pbc_compress->GetPatternNum(), DEFAULT_PATTERN_SIZE);

            int total_compressed_len = 0, total_raw_len = 0;
            int compress_pbc_only = 0, compress_secondary_only = 0, compress_pbc_combined = 0,
                compress_failed = 0;

            for (auto& test_str : raw_data_vec) {
                char* compressed_data = new char[MAX_RECORD_SIZE];
                char* decompressed_data = new char[MAX_RECORD_SIZE];
                total_raw_len += test_str.length();
                int compressed_size = pbc_compress->PBC::PBC_Compress::CompressUsingPattern(
                    const_cast<char*>(test_str.c_str()), test_str.length(), compressed_data);
                EXPECT_FALSE(PBC::PBC_isError(compressed_size));
                if (compressed_data[0] == PBC::CompressTypeFlag::COMPRESS_NOT_COMPRESS) {
                    compress_failed++;
                    total_compressed_len += test_str.length();
                } else if (compressed_data[0] == PBC::CompressTypeFlag::COMPRESS_PBC_ONLY) {
                    compress_pbc_only++;
                    total_compressed_len += compressed_size;
                } else if (compressed_data[0] == PBC::CompressTypeFlag::COMPRESS_SECONDARY_ONLY) {
                    compress_secondary_only++;
                    total_compressed_len += compressed_size;
                } else if (compressed_data[0] == PBC::CompressTypeFlag::COMPRESS_PBC_COMBINED) {
                    compress_pbc_combined++;
                    total_compressed_len += compressed_size;
                } else {
                    EXPECT_TRUE(false) << "compress error :" << compressed_size << std::endl;
                }

                int decompressed_len = pbc_compress->PBC::PBC_Compress::DecompressUsingPattern(
                    compressed_data, compressed_size, decompressed_data);
                EXPECT_NE(true, PBC::PBC_isError(decompressed_len)) << "decompress failed.";
                EXPECT_EQ(decompressed_len, test_str.length());
                EXPECT_EQ(0, memcmp(reinterpret_cast<const void*>(test_str.c_str()),
                                    decompressed_data, test_str.length()))
                    << "wrong compression and decompression,"
                    << ",compress_method:" << compress_method << ",test_str:" << test_str
                    << ",decompressed_data:" << decompressed_data;
                delete[] compressed_data;
                delete[] decompressed_data;
            }
            PBC_LOG(INFO) << "Compress Method : " << compress_method << std::endl;
            PBC_LOG(INFO) << "Test Set Compression ratio: "
                          << static_cast<double>(total_compressed_len) / total_raw_len << std::endl;
            PBC_LOG(INFO) << "compress_pbc_only rate : "
                          << compress_pbc_only / static_cast<double>(record_num) << std::endl;
            PBC_LOG(INFO) << "compress_secondary_only rate : "
                          << compress_secondary_only / static_cast<double>(record_num) << std::endl;
            PBC_LOG(INFO) << "compress_pbc_combined rate : "
                          << compress_pbc_combined / static_cast<double>(record_num) << std::endl;
            PBC_LOG(INFO) << "compress_failed rate : "
                          << compress_failed / static_cast<double>(record_num) << std::endl;
            delete pbc_compress;
            delete pbc_train;
            delete[] records_buffer;
            delete[] train_buffer;
            delete[] pattern_buffer;
        }
    }
    delete[] file_buffer;
    delete[] pattern_buffer;
}

// Test compress and decompress given datasets
TEST(PBC_CompressionTest, GivenDatasets) {
    for (const std::string& dataset : test_datasets) {
        for (PBC::CompressMethod compress_method : compress_methods) {
            for (int train_thread_num : train_thread_nums) {
                // pattern training test
                std::string test_file = FLAGS_dataset_path + dataset;
                char* original_buffer = nullptr;
                char* records_buffer = nullptr;
                char* train_buffer = nullptr;
                char* pattern_buffer = nullptr;
                int64_t original_len = 0;
                int64_t records_buffer_len = 0;
                int64_t train_buffer_len = 0;
                int64_t pattern_buffer_len = 0;
                int64_t record_num = 0;
                int32_t max_record_len = 0;

                original_len = PBC::ReadFile(test_file.c_str(), &original_buffer);
                EXPECT_GT(original_len, 0);
                records_buffer_len =
                    PBC::ReadDataFromBuffer(TYPE_RECORD, original_buffer, original_len,
                                            &records_buffer, record_num, max_record_len);
                EXPECT_GT(records_buffer_len, 0);
                train_buffer_len =
                    PBC::SamplingFromData(records_buffer, records_buffer_len, record_num,
                                          &train_buffer, record_num / SAMPLE_STEP);
                EXPECT_GT(train_buffer_len, 0);

                PBC::PBC_Train* pbc_train = new PBC::PBC_Train(compress_method, train_thread_num);
                pbc_train->PBC::PBC_Train::LoadData(train_buffer, train_buffer_len,
                                                    /*data_type=*/TYPE_VARCHAR);
                pattern_buffer_len =
                    pbc_train->PBC::PBC_Train::TrainPattern(DEFAULT_PATTERN_SIZE, &pattern_buffer);
                EXPECT_GT(pattern_buffer_len, 0);

                // compression test
                PBC::PBC_Compress* pbc_compress =
                    PBC::CompressFactory::CreatePBCCompress(compress_method);

                EXPECT_TRUE(
                    pbc_compress->PBC::PBC_Compress::ReadData(pattern_buffer, pattern_buffer_len))
                    << "read pattern failed" << std::endl;

                std::string test_str;
                std::fstream str_fstream;
                str_fstream.open(test_file, std::ios::in);
                int total_compressed_len = 0, total_raw_len = 0;
                int compress_pbc_only = 0, compress_secondary_only = 0, compress_pbc_combined = 0,
                    compress_failed = 0;

                while (getline(str_fstream, test_str)) {
                    char* compressed_data = new char[MAX_RECORD_SIZE];
                    char* decompressed_data = new char[MAX_RECORD_SIZE];
                    total_raw_len += test_str.length();
                    size_t compressed_size = pbc_compress->PBC::PBC_Compress::CompressUsingPattern(
                        const_cast<char*>(test_str.c_str()), test_str.length(), compressed_data);

                    EXPECT_FALSE(PBC::PBC_isError(compressed_size));
                    if (compressed_data[0] == PBC::CompressTypeFlag::COMPRESS_NOT_COMPRESS) {
                        compress_failed++;
                        total_compressed_len += test_str.length();
                    } else if (compressed_data[0] == PBC::CompressTypeFlag::COMPRESS_PBC_ONLY) {
                        compress_pbc_only++;
                        total_compressed_len += compressed_size;
                    } else if (compressed_data[0] ==
                               PBC::CompressTypeFlag::COMPRESS_SECONDARY_ONLY) {
                        compress_secondary_only++;
                        total_compressed_len += compressed_size;
                    } else if (compressed_data[0] == PBC::CompressTypeFlag::COMPRESS_PBC_COMBINED) {
                        compress_pbc_combined++;
                        total_compressed_len += compressed_size;
                    } else {
                        EXPECT_TRUE(false) << "compress error :" << compressed_size << std::endl;
                    }
                    size_t decompressed_len =
                        pbc_compress->PBC::PBC_Compress::DecompressUsingPattern(
                            compressed_data, compressed_size, decompressed_data);
                    EXPECT_NE(true, PBC::PBC_isError(decompressed_len)) << "decompress failed.";
                    EXPECT_EQ(decompressed_len, test_str.length());
                    EXPECT_EQ(0, memcmp(reinterpret_cast<const void*>(test_str.c_str()),
                                        decompressed_data, test_str.length()))
                        << "wrong compression and decompression,"
                        << ",compress_method:" << compress_method << ",test_str:" << test_str
                        << ",decompressed_data:" << decompressed_data;
                    delete[] compressed_data;
                    delete[] decompressed_data;
                }
                PBC_LOG(INFO) << "Compress Method : " << compress_method << std::endl;
                PBC_LOG(INFO) << "Test Set Compression ratio: "
                              << static_cast<double>(total_compressed_len) / total_raw_len
                              << std::endl;
                PBC_LOG(INFO) << "compress_pbc_only rate : "
                              << compress_pbc_only / static_cast<double>(record_num) << std::endl;
                PBC_LOG(INFO) << "compress_secondary_only rate : "
                              << compress_secondary_only / static_cast<double>(record_num)
                              << std::endl;
                PBC_LOG(INFO) << "compress_pbc_combined rate : "
                              << compress_pbc_combined / static_cast<double>(record_num)
                              << std::endl;
                PBC_LOG(INFO) << "compress_failed rate : "
                              << compress_failed / static_cast<double>(record_num) << std::endl;
                delete pbc_train;
                delete pbc_compress;
                delete[] records_buffer;
                delete[] train_buffer;
                delete[] original_buffer;
                delete[] pattern_buffer;
            }
        }
    }
}

// Test compress and decompress random data with pattern
TEST(PBC_CompressionTest, RandomDataWithPattern) {
    TestRandomDataWithPattern(false);
}

// Test compress and decompress random data with pattern containing empty char
TEST(PBC_CompressionTest, RandomDataWithPatternContainEmptyChar) {
    TestRandomDataWithPattern(true);
}
