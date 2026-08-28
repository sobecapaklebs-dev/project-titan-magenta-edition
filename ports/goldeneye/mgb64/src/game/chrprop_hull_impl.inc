/*
 * chrprop_hull_impl.inc — pure geometry of the retail collision-hull builder at
 * 0x7F03ECC0 (a NONMATCHING native reimplementation; the retail MIPS asm lives
 * in chrprop.c). Extracted into a single translation-unit-shared .inc so the
 * exact production code is exercised by the ROM-free corpus regression test
 * (tests/test_hull_builder.c) with zero risk of a drifting copy.
 *
 * `chrpropHullBuildFootprint` projects the 8 bbox corners to the XZ plane, walks
 * the 4 extreme picks (min-x / max-y / max-x / min-y, with the exact retail
 * tie-breaks) and inserts intermediate hull vertices, translates by the matrix
 * position, and returns the hull vertex count. `retail_unclamped` selects the
 * retail behavior (store the true count, up to 8) vs the port's memory-safe
 * clamp-to-4 (needed only for the bare-rect4f tank path). See FID-0132/FID-0096.
 *
 * Requires (provided by the including TU): f32/f64/s32/u8 typedefs, getenv/
 * fprintf/stderr, and hullVertexCount() (hull_vertex_clamp.h).
 */

static int chrpropHullBuildFootprint(f32 xmin, f32 xmax, f32 ymin, f32 ymax,
                                     f32 zmin, f32 zmax, const f32 m[4][4],
                                     f32 *out, int retail_unclamped) {
    f64 vertices[8][2];
    s32 remaining[8];
    u8 is_extreme[8];
    f64 m00d, m02d, m10d, m12d, m20d, m22d;
    f64 xmin_d, ymin_d, zmin_d, xmax_d, ymax_d, zmax_d;
    f64 m00_xmin, m10_ymin, m20_zmin;
    f64 m02_xmin, m12_ymin, m22_zmin;
    f64 m20_zmax, m22_zmax;
    f64 m10_ymax, m12_ymax;
    f64 m00_xmax, m02_xmax;
    f64 sum_x, sum_y;
    f64 edge_dx, edge_dy, px, py, cross1, cross2;
    f64 bx, by;
    s32 min_x_idx, max_y_idx, max_x_idx, min_y_idx;
    s32 i, j, count, remaining_count;
    s32 *rp;
    s32 *rp_end;

    m00d = (f64)m[0][0];
    m02d = (f64)m[0][2];
    m10d = (f64)m[1][0];
    m12d = (f64)m[1][2];
    m20d = (f64)m[2][0];
    m22d = (f64)m[2][2];

    xmin_d = (f64)xmin;
    ymin_d = (f64)ymin;
    zmin_d = (f64)zmin;
    xmax_d = (f64)xmax;
    ymax_d = (f64)ymax;
    zmax_d = (f64)zmax;

    m00_xmin = m00d * xmin_d;
    m10_ymin = m10d * ymin_d;
    m20_zmin = m20d * zmin_d;
    m02_xmin = m02d * xmin_d;
    m12_ymin = m12d * ymin_d;
    m22_zmin = m22d * zmin_d;
    m20_zmax = m20d * zmax_d;
    m22_zmax = m22d * zmax_d;

    /* Compute 8 projected vertices of the bounding box */
    /* Each vertex projected to 2D via matrix X-column and Z-column */

    /* vertex 0: xmin, ymin, zmin */
    sum_x = m00_xmin + m10_ymin;
    sum_y = m02_xmin + m12_ymin;
    vertices[0][0] = sum_x + m20_zmin;
    vertices[0][1] = sum_y + m22_zmin;

    /* vertex 1: xmin, ymin, zmax */
    vertices[1][0] = sum_x + m20_zmax;
    vertices[1][1] = sum_y + m22_zmax;

    m10_ymax = m10d * ymax_d;
    m12_ymax = m12d * ymax_d;

    /* vertex 2: xmin, ymax, zmin */
    sum_x = m00_xmin + m10_ymax;
    sum_y = m02_xmin + m12_ymax;
    vertices[2][0] = sum_x + m20_zmin;
    vertices[2][1] = sum_y + m22_zmin;

    /* vertex 3: xmin, ymax, zmax */
    vertices[3][0] = sum_x + m20_zmax;
    vertices[3][1] = sum_y + m22_zmax;

    m00_xmax = m00d * xmax_d;
    m02_xmax = m02d * xmax_d;

    /* vertex 4: xmax, ymin, zmin */
    sum_x = m00_xmax + m10_ymin;
    sum_y = m02_xmax + m12_ymin;
    vertices[4][0] = sum_x + m20_zmin;
    vertices[4][1] = sum_y + m22_zmin;

    /* vertex 5: xmax, ymin, zmax */
    vertices[5][0] = sum_x + m20_zmax;
    vertices[5][1] = sum_y + m22_zmax;

    /* vertex 6: xmax, ymax, zmin */
    sum_x = m00_xmax + m10_ymax;
    sum_y = m02_xmax + m12_ymax;
    vertices[6][0] = sum_x + m20_zmin;
    vertices[6][1] = sum_y + m22_zmin;

    /* vertex 7: xmax, ymax, zmax */
    vertices[7][0] = sum_x + m20_zmax;
    vertices[7][1] = sum_y + m22_zmax;

    /* Find 4 extreme vertices of the convex hull */

    /* min-x vertex ($v0/$t1 in asm), tiebreak: min y */
    min_x_idx = 0;
    for (i = 1; i < 8; i++) {
        if (vertices[i][0] < vertices[min_x_idx][0]) {
            min_x_idx = i;
        } else if (vertices[i][0] == vertices[min_x_idx][0]) {
            if (vertices[i][1] < vertices[min_x_idx][1]) {
                min_x_idx = i;
            }
        }
    }

    /* max-y vertex ($t2/$t0 in asm), tiebreak: min x */
    max_y_idx = 0;
    for (i = 1; i < 8; i++) {
        if (vertices[max_y_idx][1] < vertices[i][1]) {
            max_y_idx = i;
        } else if (vertices[i][1] == vertices[max_y_idx][1]) {
            if (vertices[i][0] < vertices[max_y_idx][0]) {
                max_y_idx = i;
            }
        }
    }

    /* max-x vertex ($a0/$a3 in asm), tiebreak: max y */
    max_x_idx = 0;
    for (i = 1; i < 8; i++) {
        if (vertices[max_x_idx][0] < vertices[i][0]) {
            max_x_idx = i;
        } else if (vertices[i][0] == vertices[max_x_idx][0]) {
            if (vertices[max_x_idx][1] < vertices[i][1]) {
                max_x_idx = i;
            }
        }
    }

    /* min-y vertex ($t3/$t4 in asm), tiebreak: max x */
    min_y_idx = 0;
    for (i = 1; i < 8; i++) {
        if (vertices[i][1] < vertices[min_y_idx][1]) {
            min_y_idx = i;
        } else if (vertices[i][1] == vertices[min_y_idx][1]) {
            if (vertices[min_y_idx][0] < vertices[i][0]) {
                min_y_idx = i;
            }
        }
    }

    /* Collect non-extreme vertex indices. The four "extreme" picks are not
     * guaranteed to be unique when several projected vertices tie on an axis. */
    for (i = 0; i < 8; i++) {
        is_extreme[i] = 0;
    }

    is_extreme[min_x_idx] = 1;
    is_extreme[max_x_idx] = 1;
    is_extreme[max_y_idx] = 1;
    is_extreme[min_y_idx] = 1;

    remaining_count = 0;
    for (i = 0; i < 8; i++) {
        if (is_extreme[i]) {
            continue;
        }
        remaining[remaining_count] = i;
        remaining_count++;
    }

    /* Build convex hull polygon, outputting vertices into rect */
    count = 1;

    /* Output vertex 0: min-x */
    out[0] = (f32)vertices[min_x_idx][0];
    out[1] = (f32)vertices[min_x_idx][1];

    /*
     * For each edge of the hull (min_x -> min_y -> max_x -> max_y -> min_x),
     * test remaining vertices with a cross product to insert between corners.
     * Cross product test: (p - B) x (A - B) < 0, where edge goes from A to B.
     */

    /* Edge 1: min_x (A) to min_y (B) */
    bx = vertices[min_y_idx][0];
    by = vertices[min_y_idx][1];
    edge_dx = vertices[min_x_idx][0] - bx;
    edge_dy = vertices[min_x_idx][1] - by;

    rp = remaining;
    rp_end = remaining + remaining_count;
    while (rp < rp_end) {
        j = *rp;
        px = vertices[j][0] - bx;
        py = vertices[j][1] - by;
        cross1 = px * edge_dy;
        cross2 = py * edge_dx;
        if (cross1 < cross2) {
            out[count * 2] = (f32)vertices[j][0];
            out[count * 2 + 1] = (f32)vertices[j][1];
            count++;
            break;
        }
        rp++;
    }

    /* Output min_y vertex */
    out[count * 2] = (f32)vertices[min_y_idx][0];
    out[count * 2 + 1] = (f32)vertices[min_y_idx][1];
    count++;

    /* Edge 2: min_y (A) to max_x (B) */
    bx = vertices[max_x_idx][0];
    by = vertices[max_x_idx][1];
    edge_dy = vertices[min_y_idx][1] - by;
    edge_dx = vertices[min_y_idx][0] - bx;

    rp = remaining;
    rp_end = remaining + remaining_count;
    while (rp < rp_end) {
        j = *rp;
        px = vertices[j][0] - bx;
        py = vertices[j][1] - by;
        cross1 = px * edge_dy;
        cross2 = py * edge_dx;
        if (cross1 < cross2) {
            out[count * 2] = (f32)vertices[j][0];
            out[count * 2 + 1] = (f32)vertices[j][1];
            count++;
            break;
        }
        rp++;
    }

    /* Output max_x vertex */
    out[count * 2] = (f32)vertices[max_x_idx][0];
    out[count * 2 + 1] = (f32)vertices[max_x_idx][1];
    count++;

    /* Edge 3: max_x (A) to max_y (B) */
    bx = vertices[max_y_idx][0];
    by = vertices[max_y_idx][1];
    edge_dx = vertices[max_x_idx][0] - bx;
    edge_dy = vertices[max_x_idx][1] - by;

    rp = remaining;
    rp_end = remaining + remaining_count;
    while (rp < rp_end) {
        j = *rp;
        px = vertices[j][0] - bx;
        py = vertices[j][1] - by;
        cross1 = px * edge_dy;
        cross2 = py * edge_dx;
        if (cross1 < cross2) {
            out[count * 2] = (f32)vertices[j][0];
            out[count * 2 + 1] = (f32)vertices[j][1];
            count++;
            break;
        }
        rp++;
    }

    /* Output max_y vertex */
    out[count * 2] = (f32)vertices[max_y_idx][0];
    out[count * 2 + 1] = (f32)vertices[max_y_idx][1];
    count++;

    /* Edge 4: max_y (A) to min_x (B) */
    bx = vertices[min_x_idx][0];
    by = vertices[min_x_idx][1];
    edge_dy = vertices[max_y_idx][1] - by;
    edge_dx = vertices[max_y_idx][0] - bx;

    rp = remaining;
    rp_end = remaining + remaining_count;
    while (rp < rp_end) {
        j = *rp;
        px = vertices[j][0] - bx;
        py = vertices[j][1] - by;
        cross1 = px * edge_dy;
        cross2 = py * edge_dx;
        if (cross1 < cross2) {
            out[count * 2] = (f32)vertices[j][0];
            out[count * 2 + 1] = (f32)vertices[j][1];
            count++;
            break;
        }
        rp++;
    }

    /* FID-0132: retail stores the true hull vertex count (up to 8) with NO clamp
     * and writes the overflow vertices into the words that follow the 4-vertex
     * rect4f. The port previously clamped to 4 unconditionally (FID-0096), which
     * DROPS real hull corners for thin / near-axis-aligned box footprints:
     * point-in-polygon then tests a triangle (or a duplicated-corner sliver)
     * instead of the true rectangle, so object grounding fails (e.g. the Dam
     * control-room monitors floor-snapping ~93 units below their desks). The
     * caller passes retail_unclamped=1 for the collision_data-backed object/door
     * path (which has room for the full hull) and 0 for the bare-rect4f tank
     * path. */
    count = hullVertexCount(count, retail_unclamped);
    if (count > 0) {
        for (i = 0; i < count; i++) {
            out[i * 2] += m[3][0];
            out[i * 2 + 1] += m[3][2];
        }
    }

    /* Census diagnostic (GE007_TRACE_HULL_DEGEN, off by default). Classifies and
     * logs every hull whose emitted footprint has fewer than 4 distinct corners,
     * separating two very different causes:
     *   INHERENT  - the projected box is collinear (a flat panel / edge-on box):
     *               it has no 2D area, so <4 corners is correct and unfixable.
     *   SPURIOUS  - the projected box has genuine 2D area (>=4-corner hull) but
     *               the emitted polygon collapsed to <4 distinct points. This is
     *               the FID-0132 defect: dropped hull corners break point-in-poly
     *               (object grounding, e.g. Dam monitors floor-snapping).
     * SPURIOUS is the blast-radius denominator the fix must drive to zero. Full
     * inputs (bbox extents + the 8 matrix elements used) are logged so the exact
     * case can be replayed offline in the corpus ctest. */
    if (getenv("GE007_TRACE_HULL_DEGEN") != NULL) {
        static long s_hull_calls = 0;
        static long s_hull_spurious = 0;
        static long s_hull_inherent = 0;
        s32 di, dj;
        s32 d_emit = 0;      /* distinct emitted points */
        s32 collinear = 1;   /* are all 8 projected vertices on one line? */
        f64 dirx = 0.0, diry = 0.0;
        s32 have_dir = 0;
        s_hull_calls++;
        for (di = 0; di < count; di++) {
            s32 dup = 0;
            for (dj = 0; dj < di; dj++) {
                if (out[dj * 2] == out[di * 2] && out[dj * 2 + 1] == out[di * 2 + 1]) { dup = 1; break; }
            }
            if (!dup) d_emit++;
        }
        for (di = 1; di < 8 && collinear; di++) {
            f64 dx = vertices[di][0] - vertices[0][0];
            f64 dy = vertices[di][1] - vertices[0][1];
            if (dx == 0.0 && dy == 0.0) continue;
            if (!have_dir) { dirx = dx; diry = dy; have_dir = 1; continue; }
            if (dx * diry - dy * dirx != 0.0) collinear = 0;
        }
        if (d_emit < 4) {
            /* A footprint with <=2 distinct emitted corners is a line segment (or
             * point): it has no 2D area, so it is inherently degenerate no matter
             * how the hull is built (the exact-arithmetic collinear test above can
             * miss this when projection matrix elements are ~1e-8 rather than
             * exactly 0). Only a >=3-distinct footprint that still lost a corner is
             * the SPURIOUS clamp defect. */
            s32 inherent = (collinear || d_emit <= 2);
            const char *label = inherent ? "INHERENT" : "SPURIOUS";
            if (inherent) s_hull_inherent++; else s_hull_spurious++;
            fprintf(stderr,
                "[HULL_DEGEN:%s] spurious=%ld inherent=%ld/%ld cnt=%d d_emit=%d "
                "in=%.4f,%.4f,%.4f,%.4f,%.4f,%.4f "
                "mtx=%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.4f,%.4f ext=%d,%d,%d,%d pts=",
                label, s_hull_spurious, s_hull_inherent, s_hull_calls, count, d_emit,
                xmin, xmax, ymin, ymax, zmin, zmax,
                m[0][0], m[0][2], m[1][0], m[1][2],
                m[2][0], m[2][2], m[3][0], m[3][2],
                min_x_idx, max_y_idx, max_x_idx, min_y_idx);
            for (di = 0; di < count; di++) {
                fprintf(stderr, "(%.4f,%.4f)", out[di * 2], out[di * 2 + 1]);
            }
            fprintf(stderr, "\n");
            fflush(stderr);
        }
    }

    return count;
}
