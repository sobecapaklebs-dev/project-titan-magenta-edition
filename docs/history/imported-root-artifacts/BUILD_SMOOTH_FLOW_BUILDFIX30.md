# Project Titan Magenta Edition RC14 — BuildFix30 Smooth Flow

Hardware/video comparison exposed the exact cause of the residual jerky Flow motion.

## Root cause

BuildFix29 computed the long sweep with `frame * 0.022f` and then truncated it into an unsigned integer LUT phase. At 60 Hz that advances one 128-entry LUT step only about every 45 rendered frames, which looks like a PowerPoint transition even while the shell remains full-rate. Several local deformation phases had similar integer quantization.

## Fix

`TitanXmbSpline_WaveFloat()` linearly interpolates adjacent entries of the existing 128-sample wave LUT. This preserves the cheap PS2 implementation but supports fractional phase on every rendered frame.

The Flow3D path now uses fractional phase for:

- global sweep/orbit
- tangent rotation
- breathing and visibility
- orbital micro-drift
- whole-ribbon vertical drift
- Gerstner carriers
- temporal noise
- flex bend
- span pulse
- point-local rotation

The renderer also morphs the two cubic-Bezier control points independently over the long sweep, so the global spine itself changes curvature instead of simply rotating a fixed curve.

Scarlet Flow is now version 3.2.0. BuildFix30 is intended to be distributed through the hardware-validated forward-only TPUP updater.
