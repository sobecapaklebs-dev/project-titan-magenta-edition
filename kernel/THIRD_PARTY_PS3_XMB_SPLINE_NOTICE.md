# Third-party notice: PlayStation-3-XMB spline study

The Project Titan XMB-style wave adaptation was informed by the following
MIT-licensed research project supplied by the user:

- Project: `PlayStation-3-XMB`
- Copyright: Copyright (c) 2025 Mart
- License: MIT
- Relevant files studied: `ps3xmbwave/spline.js`,
  `ps3xmbwave/spline-reverse.js`, `ps3xmbwave/spline-settings.js`, and
  `SPLINE_REVERSE_ENGINEER.md`

The Project Titan implementation is a PS2-specific rewrite. It does not embed
WebGL, JavaScript, the 256x64 R32F displacement texture, or PS3 binaries. It
retains the high-level pipeline concepts of a 361-entry descriptor table,
eight kernel iterations, cubic B-spline sampling, temporal smoothing, a
multi-row wave surface, and Fresnel-like opacity modulation.

MIT License

Copyright (c) 2025 Mart

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
