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

#ifndef SRC_BASE_MEMCPY_H_
#define SRC_BASE_MEMCPY_H_

#ifdef PBC_ENABLE_FAST_MEMCPY

#include "deps/memcpy/FastMemcpy.h"
#define pbc_memcpy(dst, src, size) memcpy_fast_sse(dst, src, size)

#else

#define pbc_memcpy(dst, src, size) memcpy(dst, src, size)

#endif

#endif  // SRC_BASE_MEMCPY_H_
