/* ******************************************************************
 * huff0 huffman codec,
 * part of Finite State Entropy library
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

#ifndef PBC_HUF_H_298734234
#define PBC_HUF_H_298734234

/* *** Dependencies *** */
#include <stddef.h>    /* size_t */


/* *** library symbols visibility *** */
/* Note : when linking with -fvisibility=hidden on gcc, or by default on Visual,
 *        HUF symbols remain "private" (internal symbols for library only).
 *        Set macro PBC_FSE_DLL_EXPORT to 1 if you want HUF symbols visible on DLL interface */
#if defined(PBC_FSE_DLL_EXPORT) && (PBC_FSE_DLL_EXPORT==1) && defined(__GNUC__) && (__GNUC__ >= 4)
#  define PBC_HUF_PUBLIC_API __attribute__ ((visibility ("default")))
#elif defined(PBC_FSE_DLL_EXPORT) && (PBC_FSE_DLL_EXPORT==1)   /* Visual expected */
#  define PBC_HUF_PUBLIC_API __declspec(dllexport)
#elif defined(PBC_FSE_DLL_IMPORT) && (PBC_FSE_DLL_IMPORT==1)
#  define PBC_HUF_PUBLIC_API __declspec(dllimport)  /* not required, just to generate faster code (saves a function pointer load from IAT and an indirect jump) */
#else
#  define PBC_HUF_PUBLIC_API
#endif


/* ========================== */
/* ***  simple functions  *** */
/* ========================== */

/** PBC_HUF_compress() :
 *  Compress content from buffer 'src', of size 'srcSize', into buffer 'dst'.
 * 'dst' buffer must be already allocated.
 *  Compression runs faster if `dstCapacity` >= PBC_HUF_compressBound(srcSize).
 * `srcSize` must be <= `PBC_HUF_BLOCKSIZE_MAX` == 128 KB.
 * @return : size of compressed data (<= `dstCapacity`).
 *  Special values : if return == 0, srcData is not compressible => Nothing is stored within dst !!!
 *                   if PBC_HUF_isError(return), compression failed (more details using PBC_HUF_getErrorName())
 */
PBC_HUF_PUBLIC_API size_t PBC_HUF_compress(void* dst, size_t dstCapacity,
                             const void* src, size_t srcSize);

/** PBC_HUF_decompress() :
 *  Decompress HUF data from buffer 'cSrc', of size 'cSrcSize',
 *  into already allocated buffer 'dst', of minimum size 'dstSize'.
 * `originalSize` : **must** be the ***exact*** size of original (uncompressed) data.
 *  Note : in contrast with FSE, PBC_HUF_decompress can regenerate
 *         RLE (cSrcSize==1) and uncompressed (cSrcSize==dstSize) data,
 *         because it knows size to regenerate (originalSize).
 * @return : size of regenerated data (== originalSize),
 *           or an error code, which can be tested using PBC_HUF_isError()
 */
PBC_HUF_PUBLIC_API size_t PBC_HUF_decompress(void* dst,  size_t originalSize,
                               const void* cSrc, size_t cSrcSize);


/* ***   Tool functions *** */
#define PBC_HUF_BLOCKSIZE_MAX (128 * 1024)                  /**< maximum input size for a single block compressed with PBC_HUF_compress */
PBC_HUF_PUBLIC_API size_t PBC_HUF_compressBound(size_t size);   /**< maximum compressed size (worst case) */

/* Error Management */
PBC_HUF_PUBLIC_API unsigned    PBC_HUF_isError(size_t code);       /**< tells if a return value is an error code */
PBC_HUF_PUBLIC_API const char* PBC_HUF_getErrorName(size_t code);  /**< provides error code string (useful for debugging) */


/* ***   Advanced function   *** */

/** PBC_HUF_compress2() :
 *  Same as PBC_HUF_compress(), but offers control over `maxSymbolValue` and `tableLog`.
 * `maxSymbolValue` must be <= PBC_HUF_SYMBOLVALUE_MAX .
 * `tableLog` must be `<= PBC_HUF_TABLELOG_MAX` . */
PBC_HUF_PUBLIC_API size_t PBC_HUF_compress2 (void* dst, size_t dstCapacity,
                               const void* src, size_t srcSize,
                               unsigned maxSymbolValue, unsigned tableLog);

/** PBC_HUF_compress4X_wksp() :
 *  Same as PBC_HUF_compress2(), but uses externally allocated `workSpace`.
 * `workspace` must have minimum alignment of 4, and be at least as large as PBC_HUF_WORKSPACE_SIZE */
#define PBC_HUF_WORKSPACE_SIZE ((6 << 10) + 256)
#define PBC_HUF_WORKSPACE_SIZE_U32 (PBC_HUF_WORKSPACE_SIZE / sizeof(U32))
PBC_HUF_PUBLIC_API size_t PBC_HUF_compress4X_wksp (void* dst, size_t dstCapacity,
                                     const void* src, size_t srcSize,
                                     unsigned maxSymbolValue, unsigned tableLog,
                                     void* workSpace, size_t wkspSize);

#endif   /* PBC_HUF_H_298734234 */

/* ******************************************************************
 *  WARNING !!
 *  The following section contains advanced and experimental definitions
 *  which shall never be used in the context of a dynamic library,
 *  because they are not guaranteed to remain stable in the future.
 *  Only consider them in association with static linking.
 * *****************************************************************/
#if defined(PBC_HUF_STATIC_LINKING_ONLY) && !defined(PBC_HUF_H_PBC_HUF_STATIC_LINKING_ONLY)
#define PBC_HUF_H_PBC_HUF_STATIC_LINKING_ONLY

/* *** Dependencies *** */
#include "mem.h"   /* U32 */


/* *** Constants *** */
#define PBC_HUF_TABLELOG_MAX      12      /* max runtime value of tableLog (due to static allocation); can be modified up to PBC_HUF_ABSOLUTEMAX_TABLELOG */
#define PBC_HUF_TABLELOG_DEFAULT  11      /* default tableLog value when none specified */
#define PBC_HUF_SYMBOLVALUE_MAX  255

#define PBC_HUF_TABLELOG_ABSOLUTEMAX  15  /* absolute limit of PBC_HUF_MAX_TABLELOG. Beyond that value, code does not work */
#if (PBC_HUF_TABLELOG_MAX > PBC_HUF_TABLELOG_ABSOLUTEMAX)
#  error "PBC_HUF_TABLELOG_MAX is too large !"
#endif


/* ****************************************
*  Static allocation
******************************************/
/* HUF buffer bounds */
#define PBC_HUF_CTABLEBOUND 129
#define PBC_HUF_BLOCKBOUND(size) (size + (size>>8) + 8)   /* only true when incompressible is pre-filtered with fast heuristic */
#define PBC_HUF_COMPRESSBOUND(size) (PBC_HUF_CTABLEBOUND + PBC_HUF_BLOCKBOUND(size))   /* Macro version, useful for static allocation */

/* static allocation of HUF's Compression Table */
#define PBC_HUF_CTABLE_SIZE_U32(maxSymbolValue)   ((maxSymbolValue)+1)   /* Use tables of U32, for proper alignment */
#define PBC_HUF_CTABLE_SIZE(maxSymbolValue)       (PBC_HUF_CTABLE_SIZE_U32(maxSymbolValue) * sizeof(U32))
#define PBC_HUF_CREATE_STATIC_CTABLE(name, maxSymbolValue) \
    U32 name##hb[PBC_HUF_CTABLE_SIZE_U32(maxSymbolValue)]; \
    void* name##hv = &(name##hb); \
    PBC_HUF_CElt* name = (PBC_HUF_CElt*)(name##hv)   /* no final ; */

/* static allocation of HUF's DTable */
typedef U32 PBC_HUF_DTable;
#define PBC_HUF_DTABLE_SIZE(maxTableLog)   (1 + (1<<(maxTableLog)))
#define PBC_HUF_CREATE_STATIC_DTABLEX1(DTable, maxTableLog) \
        PBC_HUF_DTable DTable[PBC_HUF_DTABLE_SIZE((maxTableLog)-1)] = { ((U32)((maxTableLog)-1) * 0x01000001) }
#define PBC_HUF_CREATE_STATIC_DTABLEX2(DTable, maxTableLog) \
        PBC_HUF_DTable DTable[PBC_HUF_DTABLE_SIZE(maxTableLog)] = { ((U32)(maxTableLog) * 0x01000001) }


/* ****************************************
*  Advanced decompression functions
******************************************/
size_t PBC_HUF_decompress4X1 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< single-symbol decoder */
#ifndef PBC_HUF_FORCE_DECOMPRESS_X1
size_t PBC_HUF_decompress4X2 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< double-symbols decoder */
#endif

size_t PBC_HUF_decompress4X_DCtx (PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< decodes RLE and uncompressed */
size_t PBC_HUF_decompress4X_hufOnly(PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize); /**< considers RLE and uncompressed as errors */
size_t PBC_HUF_decompress4X_hufOnly_wksp(PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize); /**< considers RLE and uncompressed as errors */
size_t PBC_HUF_decompress4X1_DCtx(PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< single-symbol decoder */
size_t PBC_HUF_decompress4X1_DCtx_wksp(PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize);   /**< single-symbol decoder */
#ifndef PBC_HUF_FORCE_DECOMPRESS_X1
size_t PBC_HUF_decompress4X2_DCtx(PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< double-symbols decoder */
size_t PBC_HUF_decompress4X2_DCtx_wksp(PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize);   /**< double-symbols decoder */
#endif


/* ****************************************
 *  HUF detailed API
 * ****************************************/

/*! PBC_HUF_compress() does the following:
 *  1. count symbol occurrence from source[] into table count[] using PBC_FSE_count() (exposed within "fse.h")
 *  2. (optional) refine tableLog using PBC_HUF_optimalTableLog()
 *  3. build Huffman table from count using PBC_HUF_buildCTable()
 *  4. save Huffman table to memory buffer using PBC_HUF_writeCTable()
 *  5. encode the data stream using PBC_HUF_compress4X_usingCTable()
 *
 *  The following API allows targeting specific sub-functions for advanced tasks.
 *  For example, it's possible to compress several blocks using the same 'CTable',
 *  or to save and regenerate 'CTable' using external methods.
 */
PBC_HUF_PUBLIC_API unsigned PBC_HUF_optimalTableLog(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue);
typedef struct PBC_HUF_CElt_s PBC_HUF_CElt;   /* incomplete type */
size_t PBC_HUF_buildCTable (PBC_HUF_CElt* CTable, const unsigned* count, unsigned maxSymbolValue, unsigned maxNbBits);   /* @return : maxNbBits; CTable and count can overlap. In which case, CTable will overwrite count content */
size_t PBC_HUF_writeCTable (void* dst, size_t maxDstSize, const PBC_HUF_CElt* CTable, unsigned maxSymbolValue, unsigned huffLog);
size_t PBC_HUF_compress4X_usingCTable(void* dst, size_t dstSize, const void* src, size_t srcSize, const PBC_HUF_CElt* CTable);
size_t PBC_HUF_estimateCompressedSize(const PBC_HUF_CElt* CTable, const unsigned* count, unsigned maxSymbolValue);
int PBC_HUF_validateCTable(const PBC_HUF_CElt* CTable, const unsigned* count, unsigned maxSymbolValue);

typedef enum {
   PBC_HUF_repeat_none,  /**< Cannot use the previous table */
   PBC_HUF_repeat_check, /**< Can use the previous table but it must be checked. Note : The previous table must have been constructed by PBC_HUF_compress{1, 4}X_repeat */
   PBC_HUF_repeat_valid  /**< Can use the previous table and it is assumed to be valid */
 } PBC_HUF_repeat;
/** PBC_HUF_compress4X_repeat() :
 *  Same as PBC_HUF_compress4X_wksp(), but considers using hufTable if *repeat != PBC_HUF_repeat_none.
 *  If it uses hufTable it does not modify hufTable or repeat.
 *  If it doesn't, it sets *repeat = PBC_HUF_repeat_none, and it sets hufTable to the table used.
 *  If preferRepeat then the old table will always be used if valid. */
size_t PBC_HUF_compress4X_repeat(void* dst, size_t dstSize,
                       const void* src, size_t srcSize,
                       unsigned maxSymbolValue, unsigned tableLog,
                       void* workSpace, size_t wkspSize,    /**< `workSpace` must be aligned on 4-bytes boundaries, `wkspSize` must be >= PBC_HUF_WORKSPACE_SIZE */
                       PBC_HUF_CElt* hufTable, PBC_HUF_repeat* repeat, int preferRepeat, int bmi2);

/** PBC_HUF_buildCTable_wksp() :
 *  Same as PBC_HUF_buildCTable(), but using externally allocated scratch buffer.
 * `workSpace` must be aligned on 4-bytes boundaries, and its size must be >= PBC_HUF_CTABLE_WORKSPACE_SIZE.
 */
#define PBC_HUF_CTABLE_WORKSPACE_SIZE_U32 (2*PBC_HUF_SYMBOLVALUE_MAX +1 +1)
#define PBC_HUF_CTABLE_WORKSPACE_SIZE (PBC_HUF_CTABLE_WORKSPACE_SIZE_U32 * sizeof(unsigned))
size_t PBC_HUF_buildCTable_wksp (PBC_HUF_CElt* tree,
                       const unsigned* count, U32 maxSymbolValue, U32 maxNbBits,
                             void* workSpace, size_t wkspSize);

/*! PBC_HUF_readStats() :
 *  Read compact Huffman tree, saved by PBC_HUF_writeCTable().
 * `huffWeight` is destination buffer.
 * @return : size read from `src` , or an error Code .
 *  Note : Needed by PBC_HUF_readCTable() and PBC_HUF_readDTableXn() . */
size_t PBC_HUF_readStats(BYTE* huffWeight, size_t hwSize,
                     U32* rankStats, U32* nbSymbolsPtr, U32* tableLogPtr,
                     const void* src, size_t srcSize);

/** PBC_HUF_readCTable() :
 *  Loading a CTable saved with PBC_HUF_writeCTable() */
size_t PBC_HUF_readCTable (PBC_HUF_CElt* CTable, unsigned* maxSymbolValuePtr, const void* src, size_t srcSize, unsigned *hasZeroWeights);

/** PBC_HUF_getNbBits() :
 *  Read nbBits from CTable symbolTable, for symbol `symbolValue` presumed <= PBC_HUF_SYMBOLVALUE_MAX
 *  Note 1 : is not inlined, as PBC_HUF_CElt definition is private
 *  Note 2 : const void* used, so that it can provide a statically allocated table as argument (which uses type U32) */
U32 PBC_HUF_getNbBits(const void* symbolTable, U32 symbolValue);

/*
 * PBC_HUF_decompress() does the following:
 * 1. select the decompression algorithm (X1, X2) based on pre-computed heuristics
 * 2. build Huffman table from save, using PBC_HUF_readDTableX?()
 * 3. decode 1 or 4 segments in parallel using PBC_HUF_decompress?X?_usingDTable()
 */

/** PBC_HUF_selectDecoder() :
 *  Tells which decoder is likely to decode faster,
 *  based on a set of pre-computed metrics.
 * @return : 0==PBC_HUF_decompress4X1, 1==PBC_HUF_decompress4X2 .
 *  Assumption : 0 < dstSize <= 128 KB */
U32 PBC_HUF_selectDecoder (size_t dstSize, size_t cSrcSize);

/**
 *  The minimum workspace size for the `workSpace` used in
 *  PBC_HUF_readDTableX1_wksp() and PBC_HUF_readDTableX2_wksp().
 *
 *  The space used depends on PBC_HUF_TABLELOG_MAX, ranging from ~1500 bytes when
 *  PBC_HUF_TABLE_LOG_MAX=12 to ~1850 bytes when PBC_HUF_TABLE_LOG_MAX=15.
 *  Buffer overflow errors may potentially occur if code modifications result in
 *  a required workspace size greater than that specified in the following
 *  macro.
 */
#define PBC_HUF_DECOMPRESS_WORKSPACE_SIZE (2 << 10)
#define PBC_HUF_DECOMPRESS_WORKSPACE_SIZE_U32 (PBC_HUF_DECOMPRESS_WORKSPACE_SIZE / sizeof(U32))

#ifndef PBC_HUF_FORCE_DECOMPRESS_X2
size_t PBC_HUF_readDTableX1 (PBC_HUF_DTable* DTable, const void* src, size_t srcSize);
size_t PBC_HUF_readDTableX1_wksp (PBC_HUF_DTable* DTable, const void* src, size_t srcSize, void* workSpace, size_t wkspSize);
#endif
#ifndef PBC_HUF_FORCE_DECOMPRESS_X1
size_t PBC_HUF_readDTableX2 (PBC_HUF_DTable* DTable, const void* src, size_t srcSize);
size_t PBC_HUF_readDTableX2_wksp (PBC_HUF_DTable* DTable, const void* src, size_t srcSize, void* workSpace, size_t wkspSize);
#endif

size_t PBC_HUF_decompress4X_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const PBC_HUF_DTable* DTable);
#ifndef PBC_HUF_FORCE_DECOMPRESS_X2
size_t PBC_HUF_decompress4X1_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const PBC_HUF_DTable* DTable);
#endif
#ifndef PBC_HUF_FORCE_DECOMPRESS_X1
size_t PBC_HUF_decompress4X2_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const PBC_HUF_DTable* DTable);
#endif


/* ====================== */
/* single stream variants */
/* ====================== */

size_t PBC_HUF_compress1X (void* dst, size_t dstSize, const void* src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog);
size_t PBC_HUF_compress1X_wksp (void* dst, size_t dstSize, const void* src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog, void* workSpace, size_t wkspSize);  /**< `workSpace` must be a table of at least PBC_HUF_WORKSPACE_SIZE_U32 unsigned */
size_t PBC_HUF_compress1X_usingCTable(void* dst, size_t dstSize, const void* src, size_t srcSize, const PBC_HUF_CElt* CTable);
/** PBC_HUF_compress1X_repeat() :
 *  Same as PBC_HUF_compress1X_wksp(), but considers using hufTable if *repeat != PBC_HUF_repeat_none.
 *  If it uses hufTable it does not modify hufTable or repeat.
 *  If it doesn't, it sets *repeat = PBC_HUF_repeat_none, and it sets hufTable to the table used.
 *  If preferRepeat then the old table will always be used if valid. */
size_t PBC_HUF_compress1X_repeat(void* dst, size_t dstSize,
                       const void* src, size_t srcSize,
                       unsigned maxSymbolValue, unsigned tableLog,
                       void* workSpace, size_t wkspSize,   /**< `workSpace` must be aligned on 4-bytes boundaries, `wkspSize` must be >= PBC_HUF_WORKSPACE_SIZE */
                       PBC_HUF_CElt* hufTable, PBC_HUF_repeat* repeat, int preferRepeat, int bmi2);

size_t PBC_HUF_decompress1X1 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /* single-symbol decoder */
#ifndef PBC_HUF_FORCE_DECOMPRESS_X1
size_t PBC_HUF_decompress1X2 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /* double-symbol decoder */
#endif

size_t PBC_HUF_decompress1X_DCtx (PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);
size_t PBC_HUF_decompress1X_DCtx_wksp (PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize);
#ifndef PBC_HUF_FORCE_DECOMPRESS_X2
size_t PBC_HUF_decompress1X1_DCtx(PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< single-symbol decoder */
size_t PBC_HUF_decompress1X1_DCtx_wksp(PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize);   /**< single-symbol decoder */
#endif
#ifndef PBC_HUF_FORCE_DECOMPRESS_X1
size_t PBC_HUF_decompress1X2_DCtx(PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< double-symbols decoder */
size_t PBC_HUF_decompress1X2_DCtx_wksp(PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize);   /**< double-symbols decoder */
#endif

size_t PBC_HUF_decompress1X_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const PBC_HUF_DTable* DTable);   /**< automatic selection of sing or double symbol decoder, based on DTable */
#ifndef PBC_HUF_FORCE_DECOMPRESS_X2
size_t PBC_HUF_decompress1X1_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const PBC_HUF_DTable* DTable);
#endif
#ifndef PBC_HUF_FORCE_DECOMPRESS_X1
size_t PBC_HUF_decompress1X2_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const PBC_HUF_DTable* DTable);
#endif

/* BMI2 variants.
 * If the CPU has BMI2 support, pass bmi2=1, otherwise pass bmi2=0.
 */
size_t PBC_HUF_decompress1X_usingDTable_bmi2(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const PBC_HUF_DTable* DTable, int bmi2);
#ifndef PBC_HUF_FORCE_DECOMPRESS_X2
size_t PBC_HUF_decompress1X1_DCtx_wksp_bmi2(PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize, int bmi2);
#endif
size_t PBC_HUF_decompress4X_usingDTable_bmi2(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const PBC_HUF_DTable* DTable, int bmi2);
size_t PBC_HUF_decompress4X_hufOnly_wksp_bmi2(PBC_HUF_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize, void* workSpace, size_t wkspSize, int bmi2);

#endif /* PBC_HUF_STATIC_LINKING_ONLY */

#if defined (__cplusplus)
}
#endif
