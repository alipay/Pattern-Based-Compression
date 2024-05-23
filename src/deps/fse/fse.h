/* ******************************************************************
 * FSE : Finite State Entropy codec
 * Public Prototypes declaration
 * Copyright (c) 2013-2020, Yann Collet, Facebook, Inc.
 *
 * You can contact the author at :
 * - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
****************************************************************** */

#if defined (__cplusplus)
extern "C" {
#endif

#ifndef PBC_FSE_H
#define PBC_FSE_H


/*-*****************************************
*  Dependencies
******************************************/
#include <stddef.h>    /* size_t, ptrdiff_t */


/*-*****************************************
*  PBC_FSE_PUBLIC_API : control library symbols visibility
******************************************/
#if defined(PBC_FSE_DLL_EXPORT) && (PBC_FSE_DLL_EXPORT==1) && defined(__GNUC__) && (__GNUC__ >= 4)
#  define PBC_FSE_PUBLIC_API __attribute__ ((visibility ("default")))
#elif defined(PBC_FSE_DLL_EXPORT) && (PBC_FSE_DLL_EXPORT==1)   /* Visual expected */
#  define PBC_FSE_PUBLIC_API __declspec(dllexport)
#elif defined(PBC_FSE_DLL_IMPORT) && (PBC_FSE_DLL_IMPORT==1)
#  define PBC_FSE_PUBLIC_API __declspec(dllimport) /* It isn't required but allows to generate better code, saving a function pointer load from the IAT and an indirect jump.*/
#else
#  define PBC_FSE_PUBLIC_API
#endif

/*------   Version   ------*/
#define PBC_FSE_VERSION_MAJOR    0
#define PBC_FSE_VERSION_MINOR    9
#define PBC_FSE_VERSION_RELEASE  0

#define PBC_FSE_LIB_VERSION PBC_FSE_VERSION_MAJOR.PBC_FSE_VERSION_MINOR.PBC_FSE_VERSION_RELEASE
#define PBC_FSE_QUOTE(str) #str
#define PBC_FSE_EXPAND_AND_QUOTE(str) PBC_FSE_QUOTE(str)
#define PBC_FSE_VERSION_STRING PBC_FSE_EXPAND_AND_QUOTE(PBC_FSE_LIB_VERSION)

#define PBC_FSE_VERSION_NUMBER  (PBC_FSE_VERSION_MAJOR *100*100 + PBC_FSE_VERSION_MINOR *100 + PBC_FSE_VERSION_RELEASE)
PBC_FSE_PUBLIC_API unsigned PBC_FSE_versionNumber(void);   /**< library version number; to be used when checking dll version */


/*-****************************************
*  FSE simple functions
******************************************/
/*! PBC_FSE_compress() :
    Compress content of buffer 'src', of size 'srcSize', into destination buffer 'dst'.
    'dst' buffer must be already allocated. Compression runs faster is dstCapacity >= PBC_FSE_compressBound(srcSize).
    @return : size of compressed data (<= dstCapacity).
    Special values : if return == 0, srcData is not compressible => Nothing is stored within dst !!!
                     if return == 1, srcData is a single byte symbol * srcSize times. Use RLE compression instead.
                     if PBC_FSE_isError(return), compression failed (more details using PBC_FSE_getErrorName())
*/
PBC_FSE_PUBLIC_API size_t PBC_FSE_compress(void* dst, size_t dstCapacity,
                             const void* src, size_t srcSize);

/*! PBC_FSE_decompress():
    Decompress FSE data from buffer 'cSrc', of size 'cSrcSize',
    into already allocated destination buffer 'dst', of size 'dstCapacity'.
    @return : size of regenerated data (<= maxDstSize),
              or an error code, which can be tested using PBC_FSE_isError() .

    ** Important ** : PBC_FSE_decompress() does not decompress non-compressible nor RLE data !!!
    Why ? : making this distinction requires a header.
    Header management is intentionally delegated to the user layer, which can better manage special cases.
*/
PBC_FSE_PUBLIC_API size_t PBC_FSE_decompress(void* dst,  size_t dstCapacity,
                               const void* cSrc, size_t cSrcSize);


/*-*****************************************
*  Tool functions
******************************************/
PBC_FSE_PUBLIC_API size_t PBC_FSE_compressBound(size_t size);       /* maximum compressed size */

/* Error Management */
PBC_FSE_PUBLIC_API unsigned    PBC_FSE_isError(size_t code);        /* tells if a return value is an error code */
PBC_FSE_PUBLIC_API const char* PBC_FSE_getErrorName(size_t code);   /* provides error code string (useful for debugging) */


/*-*****************************************
*  FSE advanced functions
******************************************/
/*! PBC_FSE_compress2() :
    Same as PBC_FSE_compress(), but allows the selection of 'maxSymbolValue' and 'tableLog'
    Both parameters can be defined as '0' to mean : use default value
    @return : size of compressed data
    Special values : if return == 0, srcData is not compressible => Nothing is stored within cSrc !!!
                     if return == 1, srcData is a single byte symbol * srcSize times. Use RLE compression.
                     if PBC_FSE_isError(return), it's an error code.
*/
PBC_FSE_PUBLIC_API size_t PBC_FSE_compress2 (void* dst, size_t dstSize, const void* src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog);


/*-*****************************************
*  FSE detailed API
******************************************/
/*!
PBC_FSE_compress() does the following:
1. count symbol occurrence from source[] into table count[] (see hist.h)
2. normalize counters so that sum(count[]) == Power_of_2 (2^tableLog)
3. save normalized counters to memory buffer using writeNCount()
4. build encoding table 'CTable' from normalized counters
5. encode the data stream using encoding table 'CTable'

PBC_FSE_decompress() does the following:
1. read normalized counters with readNCount()
2. build decoding table 'DTable' from normalized counters
3. decode the data stream using decoding table 'DTable'

The following API allows targeting specific sub-functions for advanced tasks.
For example, it's possible to compress several blocks using the same 'CTable',
or to save and provide normalized distribution using external method.
*/

/* *** COMPRESSION *** */

/*! PBC_FSE_optimalTableLog():
    dynamically downsize 'tableLog' when conditions are met.
    It saves CPU time, by using smaller tables, while preserving or even improving compression ratio.
    @return : recommended tableLog (necessarily <= 'maxTableLog') */
PBC_FSE_PUBLIC_API unsigned PBC_FSE_optimalTableLog(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue);

/*! PBC_FSE_normalizeCount():
    normalize counts so that sum(count[]) == Power_of_2 (2^tableLog)
    'normalizedCounter' is a table of short, of minimum size (maxSymbolValue+1).
    @return : tableLog,
              or an errorCode, which can be tested using PBC_FSE_isError() */
PBC_FSE_PUBLIC_API size_t PBC_FSE_normalizeCount(short* normalizedCounter, unsigned tableLog,
                    const unsigned* count, size_t srcSize, unsigned maxSymbolValue);

/*! PBC_FSE_NCountWriteBound():
    Provides the maximum possible size of an FSE normalized table, given 'maxSymbolValue' and 'tableLog'.
    Typically useful for allocation purpose. */
PBC_FSE_PUBLIC_API size_t PBC_FSE_NCountWriteBound(unsigned maxSymbolValue, unsigned tableLog);

/*! PBC_FSE_writeNCount():
    Compactly save 'normalizedCounter' into 'buffer'.
    @return : size of the compressed table,
              or an errorCode, which can be tested using PBC_FSE_isError(). */
PBC_FSE_PUBLIC_API size_t PBC_FSE_writeNCount (void* buffer, size_t bufferSize,
                                 const short* normalizedCounter,
                                 unsigned maxSymbolValue, unsigned tableLog);

/*! Constructor and Destructor of PBC_FSE_CTable.
    Note that PBC_FSE_CTable size depends on 'tableLog' and 'maxSymbolValue' */
typedef unsigned PBC_FSE_CTable;   /* don't allocate that. It's only meant to be more restrictive than void* */
PBC_FSE_PUBLIC_API PBC_FSE_CTable* PBC_FSE_createCTable (unsigned maxSymbolValue, unsigned tableLog);
PBC_FSE_PUBLIC_API void        PBC_FSE_freeCTable (PBC_FSE_CTable* ct);

/*! PBC_FSE_buildCTable():
    Builds `ct`, which must be already allocated, using PBC_FSE_createCTable().
    @return : 0, or an errorCode, which can be tested using PBC_FSE_isError() */
PBC_FSE_PUBLIC_API size_t PBC_FSE_buildCTable(PBC_FSE_CTable* ct, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

/*! PBC_FSE_compress_usingCTable():
    Compress `src` using `ct` into `dst` which must be already allocated.
    @return : size of compressed data (<= `dstCapacity`),
              or 0 if compressed data could not fit into `dst`,
              or an errorCode, which can be tested using PBC_FSE_isError() */
PBC_FSE_PUBLIC_API size_t PBC_FSE_compress_usingCTable (void* dst, size_t dstCapacity, const void* src, size_t srcSize, const PBC_FSE_CTable* ct);

/*!
Tutorial :
----------
The first step is to count all symbols. PBC_FSE_count() does this job very fast.
Result will be saved into 'count', a table of unsigned int, which must be already allocated, and have 'maxSymbolValuePtr[0]+1' cells.
'src' is a table of bytes of size 'srcSize'. All values within 'src' MUST be <= maxSymbolValuePtr[0]
maxSymbolValuePtr[0] will be updated, with its real value (necessarily <= original value)
PBC_FSE_count() will return the number of occurrence of the most frequent symbol.
This can be used to know if there is a single symbol within 'src', and to quickly evaluate its compressibility.
If there is an error, the function will return an ErrorCode (which can be tested using PBC_FSE_isError()).

The next step is to normalize the frequencies.
PBC_FSE_normalizeCount() will ensure that sum of frequencies is == 2 ^'tableLog'.
It also guarantees a minimum of 1 to any Symbol with frequency >= 1.
You can use 'tableLog'==0 to mean "use default tableLog value".
If you are unsure of which tableLog value to use, you can ask PBC_FSE_optimalTableLog(),
which will provide the optimal valid tableLog given sourceSize, maxSymbolValue, and a user-defined maximum (0 means "default").

The result of PBC_FSE_normalizeCount() will be saved into a table,
called 'normalizedCounter', which is a table of signed short.
'normalizedCounter' must be already allocated, and have at least 'maxSymbolValue+1' cells.
The return value is tableLog if everything proceeded as expected.
It is 0 if there is a single symbol within distribution.
If there is an error (ex: invalid tableLog value), the function will return an ErrorCode (which can be tested using PBC_FSE_isError()).

'normalizedCounter' can be saved in a compact manner to a memory area using PBC_FSE_writeNCount().
'buffer' must be already allocated.
For guaranteed success, buffer size must be at least PBC_FSE_headerBound().
The result of the function is the number of bytes written into 'buffer'.
If there is an error, the function will return an ErrorCode (which can be tested using PBC_FSE_isError(); ex : buffer size too small).

'normalizedCounter' can then be used to create the compression table 'CTable'.
The space required by 'CTable' must be already allocated, using PBC_FSE_createCTable().
You can then use PBC_FSE_buildCTable() to fill 'CTable'.
If there is an error, both functions will return an ErrorCode (which can be tested using PBC_FSE_isError()).

'CTable' can then be used to compress 'src', with PBC_FSE_compress_usingCTable().
Similar to PBC_FSE_count(), the convention is that 'src' is assumed to be a table of char of size 'srcSize'
The function returns the size of compressed data (without header), necessarily <= `dstCapacity`.
If it returns '0', compressed data could not fit into 'dst'.
If there is an error, the function will return an ErrorCode (which can be tested using PBC_FSE_isError()).
*/


/* *** DECOMPRESSION *** */

/*! PBC_FSE_readNCount():
    Read compactly saved 'normalizedCounter' from 'rBuffer'.
    @return : size read from 'rBuffer',
              or an errorCode, which can be tested using PBC_FSE_isError().
              maxSymbolValuePtr[0] and tableLogPtr[0] will also be updated with their respective values */
PBC_FSE_PUBLIC_API size_t PBC_FSE_readNCount (short* normalizedCounter,
                           unsigned* maxSymbolValuePtr, unsigned* tableLogPtr,
                           const void* rBuffer, size_t rBuffSize);

/*! Constructor and Destructor of PBC_FSE_DTable.
    Note that its size depends on 'tableLog' */
typedef unsigned PBC_FSE_DTable;   /* don't allocate that. It's just a way to be more restrictive than void* */
PBC_FSE_PUBLIC_API PBC_FSE_DTable* PBC_FSE_createDTable(unsigned tableLog);
PBC_FSE_PUBLIC_API void        PBC_FSE_freeDTable(PBC_FSE_DTable* dt);

/*! PBC_FSE_buildDTable():
    Builds 'dt', which must be already allocated, using PBC_FSE_createDTable().
    return : 0, or an errorCode, which can be tested using PBC_FSE_isError() */
PBC_FSE_PUBLIC_API size_t PBC_FSE_buildDTable (PBC_FSE_DTable* dt, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

/*! PBC_FSE_decompress_usingDTable():
    Decompress compressed source `cSrc` of size `cSrcSize` using `dt`
    into `dst` which must be already allocated.
    @return : size of regenerated data (necessarily <= `dstCapacity`),
              or an errorCode, which can be tested using PBC_FSE_isError() */
PBC_FSE_PUBLIC_API size_t PBC_FSE_decompress_usingDTable(void* dst, size_t dstCapacity, const void* cSrc, size_t cSrcSize, const PBC_FSE_DTable* dt);

/*!
Tutorial :
----------
(Note : these functions only decompress FSE-compressed blocks.
 If block is uncompressed, use memcpy() instead
 If block is a single repeated byte, use memset() instead )

The first step is to obtain the normalized frequencies of symbols.
This can be performed by PBC_FSE_readNCount() if it was saved using PBC_FSE_writeNCount().
'normalizedCounter' must be already allocated, and have at least 'maxSymbolValuePtr[0]+1' cells of signed short.
In practice, that means it's necessary to know 'maxSymbolValue' beforehand,
or size the table to handle worst case situations (typically 256).
PBC_FSE_readNCount() will provide 'tableLog' and 'maxSymbolValue'.
The result of PBC_FSE_readNCount() is the number of bytes read from 'rBuffer'.
Note that 'rBufferSize' must be at least 4 bytes, even if useful information is less than that.
If there is an error, the function will return an error code, which can be tested using PBC_FSE_isError().

The next step is to build the decompression tables 'PBC_FSE_DTable' from 'normalizedCounter'.
This is performed by the function PBC_FSE_buildDTable().
The space required by 'PBC_FSE_DTable' must be already allocated using PBC_FSE_createDTable().
If there is an error, the function will return an error code, which can be tested using PBC_FSE_isError().

`PBC_FSE_DTable` can then be used to decompress `cSrc`, with PBC_FSE_decompress_usingDTable().
`cSrcSize` must be strictly correct, otherwise decompression will fail.
PBC_FSE_decompress_usingDTable() result will tell how many bytes were regenerated (<=`dstCapacity`).
If there is an error, the function will return an error code, which can be tested using PBC_FSE_isError(). (ex: dst buffer too small)
*/

#endif  /* PBC_FSE_H */

#if defined(PBC_FSE_STATIC_LINKING_ONLY) && !defined(PBC_FSE_H_PBC_FSE_STATIC_LINKING_ONLY)
#define PBC_FSE_H_PBC_FSE_STATIC_LINKING_ONLY

/* *** Dependency *** */
#include "bitstream.h"


/* *****************************************
*  Static allocation
*******************************************/
/* FSE buffer bounds */
#define PBC_FSE_NCOUNTBOUND 512
#define PBC_FSE_BLOCKBOUND(size) ((size) + ((size)>>7) + 4 /* fse states */ + sizeof(size_t) /* bitContainer */)
#define PBC_FSE_COMPRESSBOUND(size) (PBC_FSE_NCOUNTBOUND + PBC_FSE_BLOCKBOUND(size))   /* Macro version, useful for static allocation */

/* It is possible to statically allocate FSE CTable/DTable as a table of PBC_FSE_CTable/PBC_FSE_DTable using below macros */
#define PBC_FSE_CTABLE_SIZE_U32(maxTableLog, maxSymbolValue)   (1 + (1<<((maxTableLog)-1)) + (((maxSymbolValue)+1)*2))
#define PBC_FSE_DTABLE_SIZE_U32(maxTableLog)                   (1 + (1<<(maxTableLog)))

/* or use the size to malloc() space directly. Pay attention to alignment restrictions though */
#define PBC_FSE_CTABLE_SIZE(maxTableLog, maxSymbolValue)   (PBC_FSE_CTABLE_SIZE_U32(maxTableLog, maxSymbolValue) * sizeof(PBC_FSE_CTable))
#define PBC_FSE_DTABLE_SIZE(maxTableLog)                   (PBC_FSE_DTABLE_SIZE_U32(maxTableLog) * sizeof(PBC_FSE_DTable))


/* *****************************************
 *  FSE advanced API
 ***************************************** */

unsigned PBC_FSE_optimalTableLog_internal(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue, unsigned minus);
/**< same as PBC_FSE_optimalTableLog(), which used `minus==2` */

/* PBC_FSE_compress_wksp() :
 * Same as PBC_FSE_compress2(), but using an externally allocated scratch buffer (`workSpace`).
 * PBC_FSE_WKSP_SIZE_U32() provides the minimum size required for `workSpace` as a table of PBC_FSE_CTable.
 */
#define PBC_FSE_WKSP_SIZE_U32(maxTableLog, maxSymbolValue)   ( PBC_FSE_CTABLE_SIZE_U32(maxTableLog, maxSymbolValue) + ((maxTableLog > 12) ? (1 << (maxTableLog - 2)) : 1024) )
size_t PBC_FSE_compress_wksp (void* dst, size_t dstSize, const void* src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog, void* workSpace, size_t wkspSize);

size_t PBC_FSE_buildCTable_raw (PBC_FSE_CTable* ct, unsigned nbBits);
/**< build a fake PBC_FSE_CTable, designed for a flat distribution, where each symbol uses nbBits */

size_t PBC_FSE_buildCTable_rle (PBC_FSE_CTable* ct, unsigned char symbolValue);
/**< build a fake PBC_FSE_CTable, designed to compress always the same symbolValue */

/* PBC_FSE_buildCTable_wksp() :
 * Same as PBC_FSE_buildCTable(), but using an externally allocated scratch buffer (`workSpace`).
 * `wkspSize` must be >= `(1<<tableLog)`.
 */
size_t PBC_FSE_buildCTable_wksp(PBC_FSE_CTable* ct, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog, void* workSpace, size_t wkspSize);

size_t PBC_FSE_buildDTable_raw (PBC_FSE_DTable* dt, unsigned nbBits);
/**< build a fake PBC_FSE_DTable, designed to read a flat distribution where each symbol uses nbBits */

size_t PBC_FSE_buildDTable_rle (PBC_FSE_DTable* dt, unsigned char symbolValue);
/**< build a fake PBC_FSE_DTable, designed to always generate the same symbolValue */

size_t PBC_FSE_decompress_wksp(void* dst, size_t dstCapacity, const void* cSrc, size_t cSrcSize, PBC_FSE_DTable* workSpace, unsigned maxLog);
/**< same as PBC_FSE_decompress(), using an externally allocated `workSpace` produced with `PBC_FSE_DTABLE_SIZE_U32(maxLog)` */

typedef enum {
   PBC_FSE_repeat_none,  /**< Cannot use the previous table */
   PBC_FSE_repeat_check, /**< Can use the previous table but it must be checked */
   PBC_FSE_repeat_valid  /**< Can use the previous table and it is assumed to be valid */
 } PBC_FSE_repeat;

/* *****************************************
*  FSE symbol compression API
*******************************************/
/*!
   This API consists of small unitary functions, which highly benefit from being inlined.
   Hence their body are included in next section.
*/
typedef struct {
    ptrdiff_t   value;
    const void* stateTable;
    const void* symbolTT;
    unsigned    stateLog;
} PBC_FSE_CState_t;

static void PBC_FSE_initCState(PBC_FSE_CState_t* CStatePtr, const PBC_FSE_CTable* ct);

static void PBC_FSE_encodeSymbol(PBC_BIT_CStream_t* bitC, PBC_FSE_CState_t* CStatePtr, unsigned symbol);

static void PBC_FSE_flushCState(PBC_BIT_CStream_t* bitC, const PBC_FSE_CState_t* CStatePtr);

/**<
These functions are inner components of PBC_FSE_compress_usingCTable().
They allow the creation of custom streams, mixing multiple tables and bit sources.

A key property to keep in mind is that encoding and decoding are done **in reverse direction**.
So the first symbol you will encode is the last you will decode, like a LIFO stack.

You will need a few variables to track your CStream. They are :

PBC_FSE_CTable    ct;         // Provided by PBC_FSE_buildCTable()
PBC_BIT_CStream_t bitStream;  // bitStream tracking structure
PBC_FSE_CState_t  state;      // State tracking structure (can have several)


The first thing to do is to init bitStream and state.
    size_t errorCode = PBC_BIT_initCStream(&bitStream, dstBuffer, maxDstSize);
    PBC_FSE_initCState(&state, ct);

Note that PBC_BIT_initCStream() can produce an error code, so its result should be tested, using PBC_FSE_isError();
You can then encode your input data, byte after byte.
PBC_FSE_encodeSymbol() outputs a maximum of 'tableLog' bits at a time.
Remember decoding will be done in reverse direction.
    PBC_FSE_encodeByte(&bitStream, &state, symbol);

At any time, you can also add any bit sequence.
Note : maximum allowed nbBits is 25, for compatibility with 32-bits decoders
    PBC_BIT_addBits(&bitStream, bitField, nbBits);

The above methods don't commit data to memory, they just store it into local register, for speed.
Local register size is 64-bits on 64-bits systems, 32-bits on 32-bits systems (size_t).
Writing data to memory is a manual operation, performed by the flushBits function.
    PBC_BIT_flushBits(&bitStream);

Your last FSE encoding operation shall be to flush your last state value(s).
    PBC_FSE_flushState(&bitStream, &state);

Finally, you must close the bitStream.
The function returns the size of CStream in bytes.
If data couldn't fit into dstBuffer, it will return a 0 ( == not compressible)
If there is an error, it returns an errorCode (which can be tested using PBC_FSE_isError()).
    size_t size = PBC_BIT_closeCStream(&bitStream);
*/


/* *****************************************
*  FSE symbol decompression API
*******************************************/
typedef struct {
    size_t      state;
    const void* table;   /* precise table may vary, depending on U16 */
} PBC_FSE_DState_t;


static void     PBC_FSE_initDState(PBC_FSE_DState_t* DStatePtr, PBC_BIT_DStream_t* bitD, const PBC_FSE_DTable* dt);

static unsigned char PBC_FSE_decodeSymbol(PBC_FSE_DState_t* DStatePtr, PBC_BIT_DStream_t* bitD);

static unsigned PBC_FSE_endOfDState(const PBC_FSE_DState_t* DStatePtr);

/**<
Let's now decompose PBC_FSE_decompress_usingDTable() into its unitary components.
You will decode FSE-encoded symbols from the bitStream,
and also any other bitFields you put in, **in reverse order**.

You will need a few variables to track your bitStream. They are :

PBC_BIT_DStream_t DStream;    // Stream context
PBC_FSE_DState_t  DState;     // State context. Multiple ones are possible
PBC_FSE_DTable*   DTablePtr;  // Decoding table, provided by PBC_FSE_buildDTable()

The first thing to do is to init the bitStream.
    errorCode = PBC_BIT_initDStream(&DStream, srcBuffer, srcSize);

You should then retrieve your initial state(s)
(in reverse flushing order if you have several ones) :
    errorCode = PBC_FSE_initDState(&DState, &DStream, DTablePtr);

You can then decode your data, symbol after symbol.
For information the maximum number of bits read by PBC_FSE_decodeSymbol() is 'tableLog'.
Keep in mind that symbols are decoded in reverse order, like a LIFO stack (last in, first out).
    unsigned char symbol = PBC_FSE_decodeSymbol(&DState, &DStream);

You can retrieve any bitfield you eventually stored into the bitStream (in reverse order)
Note : maximum allowed nbBits is 25, for 32-bits compatibility
    size_t bitField = PBC_BIT_readBits(&DStream, nbBits);

All above operations only read from local register (which size depends on size_t).
Refueling the register from memory is manually performed by the reload method.
    endSignal = PBC_FSE_reloadDStream(&DStream);

PBC_BIT_reloadDStream() result tells if there is still some more data to read from DStream.
PBC_BIT_DStream_unfinished : there is still some data left into the DStream.
PBC_BIT_DStream_endOfBuffer : Dstream reached end of buffer. Its container may no longer be completely filled.
PBC_BIT_DStream_completed : Dstream reached its exact end, corresponding in general to decompression completed.
PBC_BIT_DStream_tooFar : Dstream went too far. Decompression result is corrupted.

When reaching end of buffer (PBC_BIT_DStream_endOfBuffer), progress slowly, notably if you decode multiple symbols per loop,
to properly detect the exact end of stream.
After each decoded symbol, check if DStream is fully consumed using this simple test :
    PBC_BIT_reloadDStream(&DStream) >= PBC_BIT_DStream_completed

When it's done, verify decompression is fully completed, by checking both DStream and the relevant states.
Checking if DStream has reached its end is performed by :
    PBC_BIT_endOfDStream(&DStream);
Check also the states. There might be some symbols left there, if some high probability ones (>50%) are possible.
    PBC_FSE_endOfDState(&DState);
*/


/* *****************************************
*  FSE unsafe API
*******************************************/
static unsigned char PBC_FSE_decodeSymbolFast(PBC_FSE_DState_t* DStatePtr, PBC_BIT_DStream_t* bitD);
/* faster, but works only if nbBits is always >= 1 (otherwise, result will be corrupted) */


/* *****************************************
*  Implementation of inlined functions
*******************************************/
typedef struct {
    int deltaFindState;
    U32 deltaNbBits;
} PBC_FSE_symbolCompressionTransform; /* total 8 bytes */

PBC_MEM_STATIC void PBC_FSE_initCState(PBC_FSE_CState_t* statePtr, const PBC_FSE_CTable* ct)
{
    const void* ptr = ct;
    const U16* u16ptr = (const U16*) ptr;
    const U32 tableLog = PBC_MEM_read16(ptr);
    statePtr->value = (ptrdiff_t)1<<tableLog;
    statePtr->stateTable = u16ptr+2;
    statePtr->symbolTT = ct + 1 + (tableLog ? (1<<(tableLog-1)) : 1);
    statePtr->stateLog = tableLog;
}


/*! PBC_FSE_initCState2() :
*   Same as PBC_FSE_initCState(), but the first symbol to include (which will be the last to be read)
*   uses the smallest state value possible, saving the cost of this symbol */
PBC_MEM_STATIC void PBC_FSE_initCState2(PBC_FSE_CState_t* statePtr, const PBC_FSE_CTable* ct, U32 symbol)
{
    PBC_FSE_initCState(statePtr, ct);
    {   const PBC_FSE_symbolCompressionTransform symbolTT = ((const PBC_FSE_symbolCompressionTransform*)(statePtr->symbolTT))[symbol];
        const U16* stateTable = (const U16*)(statePtr->stateTable);
        U32 nbBitsOut  = (U32)((symbolTT.deltaNbBits + (1<<15)) >> 16);
        statePtr->value = (nbBitsOut << 16) - symbolTT.deltaNbBits;
        statePtr->value = stateTable[(statePtr->value >> nbBitsOut) + symbolTT.deltaFindState];
    }
}

PBC_MEM_STATIC void PBC_FSE_encodeSymbol(PBC_BIT_CStream_t* bitC, PBC_FSE_CState_t* statePtr, unsigned symbol)
{
    PBC_FSE_symbolCompressionTransform const symbolTT = ((const PBC_FSE_symbolCompressionTransform*)(statePtr->symbolTT))[symbol];
    const U16* const stateTable = (const U16*)(statePtr->stateTable);
    U32 const nbBitsOut  = (U32)((statePtr->value + symbolTT.deltaNbBits) >> 16);
    PBC_BIT_addBits(bitC, statePtr->value, nbBitsOut);
    statePtr->value = stateTable[ (statePtr->value >> nbBitsOut) + symbolTT.deltaFindState];
}

PBC_MEM_STATIC void PBC_FSE_flushCState(PBC_BIT_CStream_t* bitC, const PBC_FSE_CState_t* statePtr)
{
    PBC_BIT_addBits(bitC, statePtr->value, statePtr->stateLog);
    PBC_BIT_flushBits(bitC);
}


/* PBC_FSE_getMaxNbBits() :
 * Approximate maximum cost of a symbol, in bits.
 * Fractional get rounded up (i.e : a symbol with a normalized frequency of 3 gives the same result as a frequency of 2)
 * note 1 : assume symbolValue is valid (<= maxSymbolValue)
 * note 2 : if freq[symbolValue]==0, @return a fake cost of tableLog+1 bits */
PBC_MEM_STATIC U32 PBC_FSE_getMaxNbBits(const void* symbolTTPtr, U32 symbolValue)
{
    const PBC_FSE_symbolCompressionTransform* symbolTT = (const PBC_FSE_symbolCompressionTransform*) symbolTTPtr;
    return (symbolTT[symbolValue].deltaNbBits + ((1<<16)-1)) >> 16;
}

/* PBC_FSE_bitCost() :
 * Approximate symbol cost, as fractional value, using fixed-point format (accuracyLog fractional bits)
 * note 1 : assume symbolValue is valid (<= maxSymbolValue)
 * note 2 : if freq[symbolValue]==0, @return a fake cost of tableLog+1 bits */
PBC_MEM_STATIC U32 PBC_FSE_bitCost(const void* symbolTTPtr, U32 tableLog, U32 symbolValue, U32 accuracyLog)
{
    const PBC_FSE_symbolCompressionTransform* symbolTT = (const PBC_FSE_symbolCompressionTransform*) symbolTTPtr;
    U32 const minNbBits = symbolTT[symbolValue].deltaNbBits >> 16;
    U32 const threshold = (minNbBits+1) << 16;
    assert(tableLog < 16);
    assert(accuracyLog < 31-tableLog);  /* ensure enough room for renormalization double shift */
    {   U32 const tableSize = 1 << tableLog;
        U32 const deltaFromThreshold = threshold - (symbolTT[symbolValue].deltaNbBits + tableSize);
        U32 const normalizedDeltaFromThreshold = (deltaFromThreshold << accuracyLog) >> tableLog;   /* linear interpolation (very approximate) */
        U32 const bitMultiplier = 1 << accuracyLog;
        assert(symbolTT[symbolValue].deltaNbBits + tableSize <= threshold);
        assert(normalizedDeltaFromThreshold <= bitMultiplier);
        return (minNbBits+1)*bitMultiplier - normalizedDeltaFromThreshold;
    }
}


/* ======    Decompression    ====== */

typedef struct {
    U16 tableLog;
    U16 fastMode;
} PBC_FSE_DTableHeader;   /* sizeof U32 */

typedef struct
{
    unsigned short newState;
    unsigned char  symbol;
    unsigned char  nbBits;
} PBC_FSE_decode_t;   /* size == U32 */

PBC_MEM_STATIC void PBC_FSE_initDState(PBC_FSE_DState_t* DStatePtr, PBC_BIT_DStream_t* bitD, const PBC_FSE_DTable* dt)
{
    const void* ptr = dt;
    const PBC_FSE_DTableHeader* const DTableH = (const PBC_FSE_DTableHeader*)ptr;
    DStatePtr->state = PBC_BIT_readBits(bitD, DTableH->tableLog);
    PBC_BIT_reloadDStream(bitD);
    DStatePtr->table = dt + 1;
}

PBC_MEM_STATIC BYTE PBC_FSE_peekSymbol(const PBC_FSE_DState_t* DStatePtr)
{
    PBC_FSE_decode_t const DInfo = ((const PBC_FSE_decode_t*)(DStatePtr->table))[DStatePtr->state];
    return DInfo.symbol;
}

PBC_MEM_STATIC void PBC_FSE_updateState(PBC_FSE_DState_t* DStatePtr, PBC_BIT_DStream_t* bitD)
{
    PBC_FSE_decode_t const DInfo = ((const PBC_FSE_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    size_t const lowBits = PBC_BIT_readBits(bitD, nbBits);
    DStatePtr->state = DInfo.newState + lowBits;
}

PBC_MEM_STATIC BYTE PBC_FSE_decodeSymbol(PBC_FSE_DState_t* DStatePtr, PBC_BIT_DStream_t* bitD)
{
    PBC_FSE_decode_t const DInfo = ((const PBC_FSE_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    BYTE const symbol = DInfo.symbol;
    size_t const lowBits = PBC_BIT_readBits(bitD, nbBits);

    DStatePtr->state = DInfo.newState + lowBits;
    return symbol;
}

/*! PBC_FSE_decodeSymbolFast() :
    unsafe, only works if no symbol has a probability > 50% */
PBC_MEM_STATIC BYTE PBC_FSE_decodeSymbolFast(PBC_FSE_DState_t* DStatePtr, PBC_BIT_DStream_t* bitD)
{
    PBC_FSE_decode_t const DInfo = ((const PBC_FSE_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    BYTE const symbol = DInfo.symbol;
    size_t const lowBits = PBC_BIT_readBitsFast(bitD, nbBits);

    DStatePtr->state = DInfo.newState + lowBits;
    return symbol;
}

PBC_MEM_STATIC unsigned PBC_FSE_endOfDState(const PBC_FSE_DState_t* DStatePtr)
{
    return DStatePtr->state == 0;
}



#ifndef PBC_FSE_COMMONDEFS_ONLY

/* **************************************************************
*  Tuning parameters
****************************************************************/
/*!MEMORY_USAGE :
*  Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
*  Increasing memory usage improves compression ratio
*  Reduced memory usage can improve speed, due to cache effect
*  Recommended max value is 14, for 16KB, which nicely fits into Intel x86 L1 cache */
#ifndef PBC_FSE_MAX_MEMORY_USAGE
#  define PBC_FSE_MAX_MEMORY_USAGE 14
#endif
#ifndef PBC_FSE_DEFAULT_MEMORY_USAGE
#  define PBC_FSE_DEFAULT_MEMORY_USAGE 13
#endif
#if (PBC_FSE_DEFAULT_MEMORY_USAGE > PBC_FSE_MAX_MEMORY_USAGE)
#  error "PBC_FSE_DEFAULT_MEMORY_USAGE must be <= PBC_FSE_MAX_MEMORY_USAGE"
#endif

/*!PBC_FSE_MAX_SYMBOL_VALUE :
*  Maximum symbol value authorized.
*  Required for proper stack allocation */
#ifndef PBC_FSE_MAX_SYMBOL_VALUE
#  define PBC_FSE_MAX_SYMBOL_VALUE 255
#endif

/* **************************************************************
*  template functions type & suffix
****************************************************************/
#define PBC_FSE_FUNCTION_TYPE BYTE
#define PBC_FSE_FUNCTION_EXTENSION
#define PBC_FSE_DECODE_TYPE PBC_FSE_decode_t


#endif   /* !PBC_FSE_COMMONDEFS_ONLY */


/* ***************************************************************
*  Constants
*****************************************************************/
#define PBC_FSE_MAX_TABLELOG  (PBC_FSE_MAX_MEMORY_USAGE-2)
#define PBC_FSE_MAX_TABLESIZE (1U<<PBC_FSE_MAX_TABLELOG)
#define PBC_FSE_MAXTABLESIZE_MASK (PBC_FSE_MAX_TABLESIZE-1)
#define PBC_FSE_DEFAULT_TABLELOG (PBC_FSE_DEFAULT_MEMORY_USAGE-2)
#define PBC_FSE_MIN_TABLELOG 5

#define PBC_FSE_TABLELOG_ABSOLUTE_MAX 15
#if PBC_FSE_MAX_TABLELOG > PBC_FSE_TABLELOG_ABSOLUTE_MAX
#  error "PBC_FSE_MAX_TABLELOG > PBC_FSE_TABLELOG_ABSOLUTE_MAX is not supported"
#endif

#define PBC_FSE_TABLESTEP(tableSize) (((tableSize)>>1) + ((tableSize)>>3) + 3)


#endif /* PBC_FSE_STATIC_LINKING_ONLY */


#if defined (__cplusplus)
}
#endif
