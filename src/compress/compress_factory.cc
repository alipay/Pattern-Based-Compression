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

#include "compress/compress_factory.h"

#include "common/utils.h"
#include "compress/pbc_fse_compress.h"
#include "compress/pbc_fsst_compress.h"
#include "compress/pbc_only_compress.h"
#include "compress/pbc_zstd_compress.h"

namespace PBC {
PBC_Compress* CompressFactory::CreatePBCCompress(CompressMethod compress_method) {
    switch (compress_method) {
        case CompressMethod::PBC_ONLY:
            return new PBC_ONLY_Compress();
        case CompressMethod::PBC_FSE:
            return new PBC_FSE_Compress();
        case CompressMethod::PBC_FSST:
            return new PBC_FSST_Compress();
        case CompressMethod::PBC_ZSTD:
            return new PBC_ZSTD_Compress();
        default:
            PBC_LOG(ERROR) << "unknow compress method" << std::endl;
            return nullptr;
    }
}
}  // namespace PBC
