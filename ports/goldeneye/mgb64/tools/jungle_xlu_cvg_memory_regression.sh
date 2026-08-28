#!/bin/bash
#
# jungle_xlu_cvg_memory_regression.sh -- Guard Jungle generated-room XLU coverage-memory promotion.
#
# Captures are generated from the user's ROM and must stay local. Do not commit
# screenshots, traces, logs, or generated summaries from /tmp.
#
set -euo pipefail
cd "$(dirname "$0")/.."

GE007_XLU_CVG_REGRESSION_LABEL="Jungle generated-room XLU coverage-memory" \
GE007_XLU_CVG_REGRESSION_SLUG="jungle_xlu_cvg_memory" \
exec tools/surface_xlu_cvg_memory_regression.sh \
    --level 37 \
    --frame 240 \
    --msaa-values 0 \
    --min-changed-pct 0.25 \
    --min-promoted-rows 700 \
    --min-unpromoted-rows 700 \
    --max-default-unpromoted-rows 150 \
    "$@"
