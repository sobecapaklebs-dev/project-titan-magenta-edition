#ifndef GFX_PS2_VU0_H
#define GFX_PS2_VU0_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GFX_PS2_VU0_MAX_TRANSFORM_VERTICES 32u

/* Transform signed N64 object-space XYZ vertices by a row-vector 4x4 matrix.
 * out[i] receives clip XYZW. xyz_stride is in bytes.  On EE this uses VU0
 * macro mode (COP2); host builds execute a byte-identical scalar reference. */
int gfx_ps2_vu0_transform_i16_batch(
    float out[][4],
    const void *xyz_base,
    size_t xyz_stride,
    size_t count,
    const float matrix[4][4]
);

/* Compute four independent normalized dot3 lanes from signed N64 normals.
 * coeff[j] is one xyz direction and out[i][j] = dot(normal, coeff[j])/127.
 * Titan uses lanes 0..1 for directional lights and 2..3 for LOOKAT texgen.
 * The EE path batches the math through VU0; host is a scalar reference. */
int gfx_ps2_vu0_dot3_i8_batch(
    float out[][4],
    const void *normal_base,
    size_t normal_stride,
    size_t count,
    const float coeff[4][3]
);

/* Vector lerp used by the CPU clipper's intersection path.  The EE version
 * runs the four clip coordinates in VU0 macro mode; host builds keep a scalar
 * reference so contracts can compare exact semantics. */
void gfx_ps2_vu0_lerp4(
    float out[4],
    const float a[4],
    const float b[4],
    float t
);

#ifdef __cplusplus
}
#endif

#endif
