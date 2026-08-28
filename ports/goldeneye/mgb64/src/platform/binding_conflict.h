/* binding_conflict.h — SDL-free duplicate-binding conflict helpers, shared by
 * input_bindings.c, the Controls UI, and the conflict unit test (AUDIT-0050).
 * Operating on the raw encoded-binding array keeps the encoding single-sourced
 * and free of any SDL dependency, so the conflict/ownership policy is unit-
 * testable ROM-free.
 *
 * Encoding of one binding value: a button index 0..MAX-1, GB_AXIS_BASE+axis for
 * a trigger axis (LT/RT), or GB_NONE for unbound. */
#ifndef MGB64_BINDING_CONFLICT_H
#define MGB64_BINDING_CONFLICT_H

#ifdef __cplusplus
extern "C" {
#endif

#define GB_NONE       (-1)
#define GB_AXIS_BASE  1000

/* Index j in encoded[0..count) whose value equals `enc`, other than `exclude`,
 * or -1 if none. GB_NONE never matches (an unbound slot is not a conflict).
 * Trigger encodings (>= GB_AXIS_BASE) and button indices (< GB_AXIS_BASE) can
 * never alias, so one ownership check safely covers both input classes. */
int bindingOwnerOf(const int *encoded, int count, int enc, int exclude);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_BINDING_CONFLICT_H */
