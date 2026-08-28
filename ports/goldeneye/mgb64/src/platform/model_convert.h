/**
 * model_convert.h — N64→native model binary converter for 64-bit PC port.
 */
#ifndef _PLATFORM_MODEL_CONVERT_H_
#define _PLATFORM_MODEL_CONVERT_H_

#ifdef NATIVE_PORT

#include <bondtypes.h>

/**
 * Convert an N64 model binary to native 64-bit struct layout.
 *
 * Replaces sub_GAME_7F075A90 (PROMOTE) for the PC port.
 * Walks the N64 binary tree data, creates native ModelNode and rodata
 * structs with proper 64-bit pointers, and updates the ModelFileHeader.
 *
 * @param header   ModelFileHeader with numSwitches/numtextures already set
 * @param filedata Pointer to loaded N64 model binary (big-endian)
 */
void modelConvertN64Binary(ModelFileHeader *header, void *filedata);

/**
 * Free all native model allocations made by modelConvertN64Binary.
 *
 * On N64, model data lives in the memp pool and is bulk-freed on level
 * transitions via mempResetBank.  The PC port's calloc'd native structs
 * bypass the pool, so this function must be called when the stage pool
 * is reset to avoid leaking memory.
 */
void modelConvertFreeAll(void);

/**
 * Look up a native pointer by N64 byte offset relative to the root node.
 * Replaces raw `*(type *)((u8 *)root + N64_OFFSET)` patterns that break
 * on 64-bit.  Reads the VMA from the original N64 binary and resolves it
 * via the persisted offset map.
 */
/**
 * Tagged failure reasons for modelLookupByRootOffsetEx.
 */
typedef enum {
    LOOKUP_OK = 0,
    LOOKUP_NULL_HEADER,
    LOOKUP_OUT_OF_BOUNDS,
    LOOKUP_NULL_VMA,
    LOOKUP_BAD_VMA,
    LOOKUP_NOT_IN_MAP,
} ModelLookupResult;

void *modelLookupByRootOffset(ModelFileHeader *header, s32 offset_from_root);
void *modelLookupByRootOffsetEx(ModelFileHeader *header, s32 offset_from_root,
                                 ModelLookupResult *result_out);

/**
 * Validate a converted model tree — check for NULL rodata, NULL DL primaries,
 * deferred OP11 pointers, and other conversion gaps. Emits stats to stderr
 * in verbose mode, or unconditionally if problems are detected.
 */
void modelValidateConvertedTree(ModelFileHeader *header);

#endif /* NATIVE_PORT */
#endif /* _PLATFORM_MODEL_CONVERT_H_ */
