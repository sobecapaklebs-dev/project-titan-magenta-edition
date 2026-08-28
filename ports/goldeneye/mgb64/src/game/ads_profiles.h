#ifndef _GAME_ADS_PROFILES_H_
#define _GAME_ADS_PROFILES_H_

/*
 * ADS-2.2 — Port-side per-weapon aim-down-sights profile table.
 *
 * One accessor, adsGetProfile(ITEM_IDS), returns per-weapon ADS parameters.
 * FOV is stored as a *factor* of the runtime base FOV (Video.FovY), never as an
 * absolute value, and resolved at query time via bondviewGetBaseFovY() so it
 * tracks the user's FOV setting. WeaponStats (ROM-matched) is never touched.
 *
 * Entire feature is NATIVE_PORT-only and only consulted when g_pcAdsEnabled.
 */

#ifdef NATIVE_PORT

#include <bondconstants.h>
#include <bondtypes.h>

struct AdsProfile {
    /* <0 => yield to the analog scope path; else multiplier of the runtime base FOV. */
    f32 ads_fov_factor;
    f32 ads_in_time;    /* fixed lerp duration into ADS (s)  */
    f32 ads_out_time;   /* fixed lerp duration out of ADS (s) */
    f32 sens_mult;      /* EXTRA per-weapon look trim (1.0 when FOV-couple is the slowdown) */
    f32 spread_mult;    /* Inaccuracy multiplier at full ADS  */
    f32 ads_move_mult;  /* aimed forward-speed multiplier (M5) */
    f32 ads_strafe_mult;/* aimed strafe-speed multiplier (M5)  */
    f32 pose_yaw_rad;   /* Phase 2 sighted pose */
    f32 pose_pitch_rad;
    f32 pose_roll_rad;
    f32 pose_off_x;
    f32 pose_off_y;
    f32 pose_off_z;
    u8  is_scope;       /* true scope (sniper/camera) => yield to analog zoom */
};

/* Returns the authored row for `item` if present, otherwise a computed default.
 * The returned pointer is to function-static storage valid until the next call;
 * callers use it immediately (no caching across frames). Never returns NULL. */
const struct AdsProfile *adsGetProfile(ITEM_IDS item);

/* Resolve the absolute ADS FOV-Y (degrees) for a profile against `baseFov`.
 * Returns <= 0 when the profile is a true scope (caller must yield to the
 * existing analog get_item_in_hand_zoom() path). For iron sights, applies the
 * mild-iron clamp (>= baseFov/1.5) unless Input.AdsFaithfulZoom is set. */
f32 adsResolveFovY(const struct AdsProfile *p, f32 baseFov);

#endif /* NATIVE_PORT */

#endif /* _GAME_ADS_PROFILES_H_ */
