cmake_minimum_required(VERSION 3.16)
project(ge007 C)

# Default to Release when no build type is given. This is the CANONICAL config:
# the deterministic sim-state-hash baselines (baselines/tapes/*.expected.json,
# combat_route_capture_smoke, tape_regression) are recorded under Release, and the
# hash is floating-point-optimization-sensitive (FP contraction/reordering differs
# at -O0), so a default/empty-type build silently reddens those lanes with a
# confusing "sim diverged" message. Pinning the default here makes a bare
# `cmake -B build` reproduce the canonical hashes (FID-0061). Multi-config
# generators (Xcode/VS) ignore this and choose per-build. Deeper cross-config
# reproducibility (strict-FP flags) is tracked separately in FID-0061.
get_property(_ge007_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _ge007_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (canonical: Release)" FORCE)
    message(STATUS "ge007: CMAKE_BUILD_TYPE not set — defaulting to Release (canonical sim-hash config)")
endif()

# macOS-only native Metal rendering backend (opt-in; GL stays default). The
# backend TU is Objective-C++ against the system Metal/QuartzCore frameworks —
# no metal-cpp vendoring. Guarded so Linux/Windows are untouched.
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
endif()

# Product version, single-sourced for all platforms. Release CI passes
# -DMGB64_VERSION=vX.Y.Z (see .github/workflows/release.yml); dev builds default
# to 0.0.0-dev, which the in-app update check treats as "never newer" so dev
# builds never show the update banner. The derived MGB64_VER_STR is compiled into
# the app shell (MGB64_APP_VERSION_STR — used for the version display + the GitHub
# release comparison) and, on Windows, into the VERSIONINFO resource below.
set(MGB64_VERSION "0.0.0-dev" CACHE STRING
    "Product version (e.g. 0.3.3 or v0.3.3); release CI threads it through -DMGB64_VERSION")
string(REGEX REPLACE "^[vV]" "" MGB64_VER_STR "${MGB64_VERSION}")
if(MGB64_VER_STR STREQUAL "" OR MGB64_VER_STR STREQUAL "dev")
    set(MGB64_VER_STR "0.0.0-dev")
endif()

# Windows executable icon + version info (windows/mgb64.rc). The .rc embeds the
# app icon (branding/appicon-windows.ico) and a VERSIONINFO block; the latter is
# what Explorer's Properties>Details and Windows game launchers read for the
# product name + version when the exe is added to a library.
if(WIN32)
    enable_language(RC)

    # MGB64_VER_STR is derived above (all-platform). Here it feeds the Windows
    # VERSIONINFO resource: the numeric core drives the 4-part FILEVERSION.
    # Numeric core = leading dotted digits before any pre-release suffix, padded
    # to the 4-part FILEVERSION/PRODUCTVERSION shape (major,minor,patch,build).
    string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" _mgb_num "${MGB64_VER_STR}")
    if(_mgb_num STREQUAL "")
        set(_mgb_num "0")
    endif()
    string(REPLACE "." ";" _mgb_parts "${_mgb_num}")
    list(LENGTH _mgb_parts _mgb_n)
    while(_mgb_n LESS 4)
        list(APPEND _mgb_parts 0)
        list(LENGTH _mgb_parts _mgb_n)
    endwhile()
    list(GET _mgb_parts 0 1 2 3 _mgb_csv4)
    string(REPLACE ";" "," MGB64_VER_CSV "${_mgb_csv4}")
    message(STATUS "MGB64: Windows VERSIONINFO ${MGB64_VER_STR} (${MGB64_VER_CSV})")

    # Subsystem: game launchers (and Explorer double-click) start the exe with
    # no inherited console, so a console-subsystem build pops an empty console
    # window on every launch — poor presentation for a windowed game. Default to
    # the GUI subsystem (-mwindows) for a clean launch; MGB64_WIN_CONSOLE=ON
    # restores the console subsystem for debugging (live stdout without shell
    # redirection). Log capture is unaffected either way: DiagLog's CRT-fd tee
    # (src/app/diag_log.cpp) redirects stdout/stderr into mgb64.log in both
    # modes — under -mwindows it *creates* fds 1/2 via _dup2(pipe) rather than
    # duplicating the (invalid) console fds. See docs/WINDOWS_CONFIDENCE.md §6.
    option(MGB64_WIN_CONSOLE "Windows: link the console subsystem (default: GUI, -mwindows)" OFF)
endif()

set(CMAKE_C_STANDARD 11)
include(CTest)

# The portable in-process app shell (Dear ImGui launcher + in-game overlay).
# Default ON: the shipping `ge007` binary boots the launcher and delegates
# automation/CLI invocations to the unchanged engine path. -DMGB64_APP=OFF
# builds the bare engine CLI (useful for minimal/headless builds).
option(MGB64_APP "Build the in-process ImGui app shell (portable launcher + overlay)" ON)
# Build for PortMaster on GLES3 ARM handhelds (R36S/RG353* etc.).
# Swaps glad+OpenGL3.3 for GLES3.2, changes shader version to 320 es,
# and forces 640x480 fullscreen. Implies MGB64_PORTMASTER.
option(MGB64_PORTMASTER_GLES "Build for PortMaster/ARM GLES3 (R36S etc.)" OFF)

# WebGPU (wgpu-native) — the single cross-platform render backend that replaces
# the GL+Metal fork (see docs/design/WEBGPU_BACKEND_STATUS_2026-07-13.md).
# ON by default (the deliberate flip): WebGPU is now the default backend, with
# OpenGL retained as the one-release fallback via GE007_RENDERER=gl (and Metal
# via GE007_RENDERER=metal, still used by --remaster). Set -DMGB64_WEBGPU_BACKEND=OFF
# to build a GL/Metal-only binary with no wgpu dependency. cmake/webgpu.cmake
# downloads + SHA-256-verifies the pinned per-platform wgpu-native prebuilt.
option(MGB64_WEBGPU_BACKEND "Build the WebGPU (wgpu-native) render backend (default backend)" ON)
include(cmake/webgpu.cmake)

# WebGPU spike: a headless, self-validating proof that the backend works — it
# initializes WebGPU, compiles a WGSL shader AT RUNTIME (the capability that made
# WebGPU the right fit vs bgfx), renders a triangle to an offscreen target, reads
# the pixels back, and ASSERTS the triangle is present. Registered as a ctest.
if(MGB64_WEBGPU_BACKEND)
    add_executable(test_webgpu_spike tests/test_webgpu_spike.c)
    target_link_libraries(test_webgpu_spike PRIVATE webgpu)
    target_compile_features(test_webgpu_spike PRIVATE c_std_11)
    add_test(NAME webgpu_spike COMMAND test_webgpu_spike)
endif()
# -mno-ms-bitfields is an x86-family flag (it disables a layout that only
# exists there); ARM/RISC-V compilers reject it outright.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86|X86|x86)$")
    set(MGB64_MS_BITFIELD_FLAG -mno-ms-bitfields)
else()
    set(MGB64_MS_BITFIELD_FLAG "")
endif()
if(MGB64_APP)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(APPLE)
        enable_language(OBJC)  # nfd_cocoa.m (native file dialog)
    endif()
endif()

# The build uses GCC/Clang-style flags throughout (-Wall, -Wno-*,
# -fms-extensions); MSVC (cl.exe) rejects them. On Windows, build with
# MSYS2 MinGW64 as documented in docs/BUILDING.md.
if(MSVC)
    message(FATAL_ERROR
        "MSVC is not supported yet. On Windows, use the MSYS2 MinGW 64-bit "
        "toolchain (see docs/BUILDING.md).")
endif()

# --- Platform detection ---
if(EMSCRIPTEN)
    # Web build: SDL2 and WebGPU are supplied by Emscripten ports at
    # compile/link time (-sUSE_SDL=2 / --use-port=emdawnwebgpu), not by
    # find_package. No system GL either (the WebGPU backend is the only
    # renderer under wasm). Nothing to discover here.
elseif(APPLE)
    find_library(OPENGL_LIB OpenGL)
    # SDL2 via Homebrew or framework
    find_package(SDL2 QUIET)
    if(NOT SDL2_FOUND)
        # Fallback: try pkg-config
        find_package(PkgConfig)
        if(PkgConfig_FOUND)
            pkg_check_modules(SDL2 REQUIRED sdl2)
        endif()
    endif()
elseif(WIN32)
    find_package(OpenGL REQUIRED)
    find_package(SDL2 REQUIRED)
else()
    find_package(OpenGL REQUIRED)
    find_package(SDL2 REQUIRED)
endif()

option(PORT_VALIDATION_TESTS "Register runtime validation smoke tests with CTest" OFF)

if(BUILD_TESTING)
    # The internal fidelity program (docs/fidelity/, tools/fidelity/,
    # baselines/tapes/) is export-ignored out of the public source archive and
    # the fresh public-launch repo (D1, docs/design/FIDELITY_REVIEW_AND_PLAN_2026-07-10.md;
    # .gitattributes is the source of truth). Gate every ctest registration that
    # references tools/fidelity/** behind this so a checkout without that tree
    # (i.e. the public archive) simply doesn't register those tests, the same
    # "absent prereq -> not configured" convention add_port_validation_smoke
    # uses below for a missing ROM.
    #
    # Gate on a specific load-bearing FILE, not the directory (2026-07-10,
    # CR-1 fix). `git archive` / GitHub "Download ZIP" still emits a bare,
    # EMPTY tools/fidelity/ directory entry for an export-ignored tree, and
    # `tar -x`/unzip materializes that empty directory on extraction -- so
    # `if(EXISTS tools/fidelity)` is TRUE in exactly the public-consumer
    # checkout this gate exists to protect, registering ~10 fidelity ctests
    # against files that were never extracted (all ~9 then FAIL, one SKIPs,
    # naming this internal program's file layout in the output). ledger.py is
    # the file every fidelity ctest below ultimately depends on, so its
    # presence is the correct proxy for "this is a real internal checkout".
    if(EXISTS "${CMAKE_SOURCE_DIR}/tools/fidelity/ledger.py")
        set(GE007_HAVE_FIDELITY_PROGRAM TRUE)
    else()
        set(GE007_HAVE_FIDELITY_PROGRAM FALSE)
    endif()

    # IM-1 (2026-07-10 fidelity-leak review): fail loudly, not silently, if an
    # INTERNAL checkout ever loses the fidelity tree. Pre-CR-1, a checkout
    # missing tools/fidelity hard-failed ~10 ctests with missing-file errors;
    # post-CR-1 it configures clean and those ctests just don't register --
    # indistinguishable from a genuine public checkout unless something checks
    # for exactly this. docs/design/FIDELITY_REVIEW_AND_PLAN_2026-07-10.md is
    # export-ignored the same way tools/fidelity/ is, so it is absent from
    # every public archive/launch-repo checkout (only a bare empty docs/design/
    # directory entry survives extraction there, not this file) and present in
    # every real internal checkout -- so its presence, paired with the
    # fidelity tree's absence, can only mean an internal checkout lost
    # tools/fidelity/ (a bad rebase, an overzealous cleanup, ...), not that
    # this is a legitimate public/export-filtered tree.
    if(EXISTS "${CMAKE_SOURCE_DIR}/docs/design/FIDELITY_REVIEW_AND_PLAN_2026-07-10.md" AND NOT GE007_HAVE_FIDELITY_PROGRAM)
        message(FATAL_ERROR
            "internal checkout has docs/design/FIDELITY_REVIEW_AND_PLAN_2026-07-10.md "
            "but is missing tools/fidelity/ledger.py -- refusing to silently drop the "
            "fidelity ctest surface (ledger/asm-audit/hash-coverage ratchet). Restore "
            "tools/fidelity/ (e.g. `git checkout -- tools/fidelity` or re-clone).")
    endif()

    add_test(
        NAME port_native_switch_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_native_switch_access.py"
    )
    add_test(
        NAME port_release_ready_guard
        COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/ci/check_release_ready.sh"
    )
    # Guarded public-publish entrypoint (scripts/publish_public.sh) refusal-path
    # unit tests (ROM-free): dirty-tree / red-guard / missing-or-non-green-verify
    # / missing-gameplay / force-push / non-fast-forward refusals, plus a
    # happy-path dry-run and a real push to a LOCAL bare fake remote. Never
    # touches origin or the network -- guards are stubbed and the only push
    # target is a tempdir bare repo (see the script header).
    add_test(
        NAME publish_public_guard
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/tests/test_publish_public.sh"
    )
    set_tests_properties(publish_public_guard PROPERTIES TIMEOUT 120)
    # Structural pre-push leak-class gate (.githooks/pre-push) refusal proof
    # (ROM-free): a scratch repo pushes to a LOCAL bare fake public remote wired
    # with the real tracked hooks; a clean topic branch is allowed while planted
    # private-path / personal-email / credential commits are refused at the git
    # layer. Never touches origin or the network.
    add_test(
        NAME pre_push_leak_guard
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/tests/test_pre_push_hook.sh"
    )
    set_tests_properties(pre_push_leak_guard PROPERTIES TIMEOUT 120)
    # (AUDIT-0052) Release-provenance binding: commit-stamped per-asset sidecars
    # (scripts/release/stamp_provenance.sh) + the fail-closed verifier
    # (scripts/release/verify_provenance.sh) that binds every dist/ asset digest
    # to the verified source commit before upload. ROM-free: builds fixture dist/
    # trees in a tempdir and proves only a fully commit-bound set is accepted
    # while wrong-sha / wrong-commit / wrong-version / modified / missing-sidecar /
    # extra-unstamped / orphan-sidecar are all rejected.
    add_test(
        NAME release_provenance_guard
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/tests/test_release_provenance.sh"
    )
    set_tests_properties(release_provenance_guard PROPERTIES TIMEOUT 120)
    # (AUDIT-0052 signing layer) Manifest-signing scaffold: the user-side
    # verifier (scripts/release/verify_release.sh) + the fail-fast
    # release.sh --sign-manifest gate. ROM-free and secret-free: generates an
    # EPHEMERAL minisign keypair in a tempdir, signs a fixture manifest +
    # SHA256SUMS, and proves the verifier accepts only the intact signed set
    # while tampered manifest / checksums / asset, the committed PLACEHOLDER
    # pubkey, and a missing minisign binary are all rejected fail-closed, and
    # that --sign-manifest without MGB64_MANIFEST_SIGNING_KEY fails fast before
    # any build. Skips cleanly when minisign is not installed.
    add_test(
        NAME manifest_signing_guard
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/tests/test_manifest_signing.sh"
    )
    set_tests_properties(manifest_signing_guard PROPERTIES TIMEOUT 120)
    # (AUDIT-0015) Hashtable-generator fail-closed gate: scripts/make/build_hashtable.sh
    # must emit oracle-exact `md5,section,path` rows for u/j/e fixtures and exit
    # nonzero leaving NO completed CSV on every failure path (unsupported/missing
    # version, missing build dir, empty required class, extractor failure incl. a
    # silent no-output objcopy, checksum failure, unwritable destination). ROM-free
    # and MIPS-toolchain-free: PATH-shimmed fake objcopy + independently computed
    # digest oracle; the generator runs under BOTH /bin/bash 3.2 and the PATH bash.
    add_test(
        NAME build_hashtable_guard
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/tests/test_build_hashtable.sh"
    )
    set_tests_properties(build_hashtable_guard PROPERTIES TIMEOUT 120)
    # Combat/floor oracle (FID-0032) schema + comparator contract (ROM-free).
    add_test(
        NAME combat_oracle_contract
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/combat_route_contract_smoke.sh" --gate
    )
    # FID-0075 gate-routes load-robustness decision proofs (ROM-free): the
    # gate_routes_smoke agreement logic must (a) still hard-FAIL a genuine
    # consistent divergence and (b) downgrade a non-reproducible one-off load
    # flake to an unstable-note (green), so the release-gating lane stops
    # false-reddening runway_traverse under aggregate verify_all load.
    add_test(
        NAME gate_routes_smoke_selftest
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/fidelity/gate_routes_smoke.sh" --self-test
    )
    set_tests_properties(gate_routes_smoke_selftest PROPERTIES TIMEOUT 60)
    # FID-0062 sim-tick alignment regression lane (ROM-free): --align tick pairs the
    # native (1 rec/tick) vs ares (~2 rec/tick + empty-roster interleave) streams by
    # sim-tick with no phantom divergences, while --align move (index) skews ~2x.
    # Fail-on-revert: reverting the tick branch to index pairing reddens this.
    add_test(
        NAME combat_comparator_align_unittest
        COMMAND python3 -m unittest tools.tests.test_compare_combat_trace
    )
    set_tests_properties(
        combat_comparator_align_unittest
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 30
    )
    # Movement-lane twin of the FID-0062 lane (DAM_PARITY_DEEP_DIVE 2026-07-17
    # §3.1, ROM-free): --align move-global dedupes to the last record per
    # move.global and pairs onset-rebased sim-ticks, so the native per-frame vs
    # stock per-tick cadence mismatch no longer manufactures divergences (the
    # false "sprint regression"). Fail-on-revert: index pairing reddens this.
    add_test(
        NAME movement_comparator_align_unittest
        COMMAND python3 -m unittest tools.tests.test_compare_movement_trace
    )
    set_tests_properties(
        movement_comparator_align_unittest
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 30
    )
    # FID-0063 RNG call-count phase lane (ROM-free): pins the PRNG stepper
    # (bit-exact to src/random.c randomGetNext, golden vectors from the C body)
    # and the per-tick call-count extraction (segment gate at g_GlobalTimer
    # resets, last-record-per-tick sampling, common-tick-span differentials)
    # of tools/fidelity/rng_callcount_diff.py.
    add_test(
        NAME rng_callcount_diff_unittest
        COMMAND python3 -m unittest tools.tests.test_rng_callcount_diff
    )
    set_tests_properties(
        rng_callcount_diff_unittest
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 30
    )
    # AUDIT-0019 perf-gate two-policy contract (ROM-free): the portable
    # absolute-floor gate must pass a host-variance delta vs another machine's
    # cold baseline, while relative regression only enforces under --regression
    # with a matching --fingerprint (mismatch/absent -> skip, never a false
    # regression verdict); the 60fps floor + data presence stay hard in every mode.
    add_test(
        NAME perf_budget_check_unittest
        COMMAND python3 -m unittest tools.tests.test_perf_budget_check
    )
    set_tests_properties(
        perf_budget_check_unittest
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 60
    )
    # AUDIT-0005 engine ROM-loader reject gate (ROM-free, binary-dependent —
    # self-skips if no ge007 is built): a wrong 12 MB file (bad N64 magic, or
    # valid magic but non-GoldenEye title) must exit nonzero with "Refusing to
    # boot" BEFORE any SDL/audio/file-table-patch work. Fixtures are synthetic
    # sparse 12 MB files with crafted headers — no copyrighted ROM bytes.
    add_test(
        NAME port_rom_reject_guard
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/tests/test_rom_reject.sh"
    )
    set_tests_properties(port_rom_reject_guard PROPERTIES TIMEOUT 120)
    # AUDIT-0055 end-to-end config-persistence matrix through the real binary
    # (ROM-free: only touches --config-set/--dump-config/--faithful, never ROM
    # startup). Two-launch env-override shadow, render-scale clamp, faithful
    # preset, and full round-trip/reset/unknown-key preservation. Binary-dependent
    # — self-skips (exit 77) when no ge007 is built.
    add_test(
        NAME port_config_roundtrip
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/config_roundtrip_check.py"
    )
    set_tests_properties(port_config_roundtrip PROPERTIES
        TIMEOUT 120
        SKIP_RETURN_CODE 77
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    # AUDIT-0040 synthetic frame-counter screenshot harness: proves the manual/
    # auto BMP capture path reads the DEFINED frame currently presented, not a
    # stale one. GE007_SYNTH_FRAME_PATTERN fills each composited frame with a
    # solid color encoding a per-presented-frame counter; the harness boots N
    # consecutive --screenshot-frame captures under desktop GL, decodes the
    # counter from each BMP, and asserts uniform-scene + distinct + strictly
    # increasing values. Desktop GL exercises the same decode plumbing the owner
    # runs on the MGB64_PORTMASTER_GLES device (where the fix under test is the
    # pre-swap stash gfx_opengl_capture_default_framebuffer -> platformSaveScreenshot).
    # ROM-gated + binary-dependent: self-skips (exit 125) when no ROM/binary.
    add_test(
        NAME port_synth_frame_screenshot
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/synth_frame_screenshot_harness.sh"
            --no-build --start-frame 40 --count 3
    )
    set_tests_properties(port_synth_frame_screenshot PROPERTIES
        TIMEOUT 300
        SKIP_RETURN_CODE 125
        LABELS "screenshot"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(
        NAME port_public_python_tool_syntax
        COMMAND python3 -m py_compile
            "${CMAKE_SOURCE_DIR}/tools/audit_oracle_trace.py"
            "${CMAKE_SOURCE_DIR}/tools/audit_intro_trace.py"
            "${CMAKE_SOURCE_DIR}/tools/audit_minimap_dump.py"
            "${CMAKE_SOURCE_DIR}/tools/audit_render_trace.py"
            "${CMAKE_SOURCE_DIR}/tools/audit_screenshot_health.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_audio.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_audio_reference.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_intro_trace.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_movement_trace.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_combat_trace.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_screenshots.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_state.py"
            "${CMAKE_SOURCE_DIR}/tools/intro_trace_summary.py"
            "${CMAKE_SOURCE_DIR}/tools/rom_oracle_route.py"
            "${CMAKE_SOURCE_DIR}/tools/summarize_intro_census.py"
            "${CMAKE_SOURCE_DIR}/tools/summarize_build_warnings.py"
            "${CMAKE_SOURCE_DIR}/tools/assert_stub_hits_zero.py"
            "${CMAKE_SOURCE_DIR}/tools/audit_hidden_guard_contract.py"
            "${CMAKE_SOURCE_DIR}/tools/audit_knife_impact.py"
            "${CMAKE_SOURCE_DIR}/tools/campaign_route_smoke.py"
            "${CMAKE_SOURCE_DIR}/tools/route_target_reach.py"
            "${CMAKE_SOURCE_DIR}/tools/check_markdown_links.py"
            "${CMAKE_SOURCE_DIR}/tools/check_shell_syntax.py"
            "${CMAKE_SOURCE_DIR}/tools/check_github_branch_protection.py"
            "${CMAKE_SOURCE_DIR}/tools/check_github_public_commit_refs.py"
            "${CMAKE_SOURCE_DIR}/tools/check_public_history_paths.py"
            "${CMAKE_SOURCE_DIR}/tools/check_third_party_notices.py"
            "${CMAKE_SOURCE_DIR}/tools/check_sdk_inventory.py"
            "${CMAKE_SOURCE_DIR}/tools/check_native_switch_access.py"
            "${CMAKE_SOURCE_DIR}/tools/check_native_sdk_surface.py"
            "${CMAKE_SOURCE_DIR}/tools/check_native_stub_surface.py"
            "${CMAKE_SOURCE_DIR}/tools/check_bg_visibility_opcode_table.py"
            "${CMAKE_SOURCE_DIR}/tools/analyze_stock_rdp_command_stream.py"
            "${CMAKE_SOURCE_DIR}/tools/check_stock_rdp_command_stream_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/analyze_stock_rdp_pixel_probe.py"
            "${CMAKE_SOURCE_DIR}/tools/check_stock_rdp_pixel_probe_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/analyze_glass_center_handoff.py"
            "${CMAKE_SOURCE_DIR}/tools/check_glass_center_handoff_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/summarize_glass_handoff_points.py"
            "${CMAKE_SOURCE_DIR}/tools/check_glass_handoff_points_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/compare_glass_handoff_runs.py"
            "${CMAKE_SOURCE_DIR}/tools/check_glass_handoff_run_compare_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/summarize_native_tri_pixel_probe.py"
            "${CMAKE_SOURCE_DIR}/tools/check_native_tri_pixel_probe_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/check_webgpu_live_readback_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/analyze_room_glass_source_reconstruction.py"
            "${CMAKE_SOURCE_DIR}/tools/check_room_glass_source_reconstruction_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/check_rdp_coverage_memory_backend_guard.py"
            "${CMAKE_SOURCE_DIR}/tools/check_room_xlu_cvg_memory_classifier_guard.py"
            "${CMAKE_SOURCE_DIR}/tools/summarize_rdp_render_mode_trace.py"
            "${CMAKE_SOURCE_DIR}/tools/check_rdp_render_mode_trace_summary_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/select_glass_shard_pixel_targets.py"
            "${CMAKE_SOURCE_DIR}/tools/check_glass_shard_pixel_target_selection_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/check_glass_shard_scale_regression.py"
            "${CMAKE_SOURCE_DIR}/tools/export_github_launch_items.py"
            "${CMAKE_SOURCE_DIR}/tools/check_validation_smoke_registry.py"
            "${CMAKE_SOURCE_DIR}/tools/gen_env_reference.py"
            "${CMAKE_SOURCE_DIR}/macos/Scripts/generate_app_icon.py"
    )
    # tools/fidelity/** syntax coverage lives in its own registration (not the
    # "public" list above): that tree is export-ignored from the public
    # archive/launch repo, so this test is gated the same way.
    if(GE007_HAVE_FIDELITY_PROGRAM)
        add_test(
            NAME fidelity_python_tool_syntax
            COMMAND python3 -m py_compile
                "${CMAKE_SOURCE_DIR}/tools/fidelity/pixel_normalize.py"
                "${CMAKE_SOURCE_DIR}/tools/fidelity/pixel_diff.py"
                "${CMAKE_SOURCE_DIR}/tools/fidelity/sense_trace_candidates.py"
                "${CMAKE_SOURCE_DIR}/tools/fidelity/sense_pixel_candidates.py"
                "${CMAKE_SOURCE_DIR}/tools/fidelity/tests/gen_pixel_calibration.py"
                "${CMAKE_SOURCE_DIR}/tools/fidelity/tests/test_pixel_diff.py"
                "${CMAKE_SOURCE_DIR}/tools/fidelity/tests/test_sense_trace_candidates.py"
                "${CMAKE_SOURCE_DIR}/tools/fidelity/tests/test_sense_pixel_candidates.py"
                "${CMAKE_SOURCE_DIR}/tools/fidelity/tests/test_ledger.py"
        )
        set_tests_properties(fidelity_python_tool_syntax PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endif()
    add_test(
        NAME port_stock_rdp_command_stream_analyzer_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_stock_rdp_command_stream_regression.py"
    )
    add_test(
        NAME port_stock_rdp_pixel_probe_analyzer_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_stock_rdp_pixel_probe_regression.py"
    )
    add_test(
        NAME port_glass_center_handoff_analyzer_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_glass_center_handoff_regression.py"
    )
    add_test(
        NAME port_glass_handoff_points_summary_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_glass_handoff_points_regression.py"
    )
    add_test(
        NAME port_glass_handoff_run_compare_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_glass_handoff_run_compare_regression.py"
    )
    add_test(
        NAME port_bg_visibility_opcode_table_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_bg_visibility_opcode_table.py"
                --repo-root "${CMAKE_SOURCE_DIR}"
    )
    add_test(
        NAME port_native_tri_pixel_probe_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_native_tri_pixel_probe_regression.py"
    )
    add_test(
        NAME port_webgpu_live_readback_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_webgpu_live_readback_regression.py"
    )
    add_test(
        NAME port_room_glass_source_reconstruction_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_room_glass_source_reconstruction_regression.py"
    )
    # Pillow is an optional dev dep: the guard exits 125 (SKIP) when it's absent.
    set_tests_properties(port_room_glass_source_reconstruction_guard PROPERTIES SKIP_RETURN_CODE 125)
    add_test(
        NAME port_rdp_coverage_memory_backend_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_rdp_coverage_memory_backend_guard.py"
    )
    add_test(
        NAME port_room_xlu_cvg_memory_classifier_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_room_xlu_cvg_memory_classifier_guard.py"
    )
    add_test(
        NAME port_rdp_render_mode_trace_summary_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_rdp_render_mode_trace_summary_regression.py"
    )
    add_test(
        NAME port_glass_shard_pixel_target_selection_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_glass_shard_pixel_target_selection_regression.py"
    )
    # Pillow is an optional dev dep: the guard exits 125 (SKIP) when it's absent.
    set_tests_properties(port_glass_shard_pixel_target_selection_guard PROPERTIES SKIP_RETURN_CODE 125)
    # FID-0003: shard geometry scale = 1/visibility, coupled to field_10E0
    # (retail-ASM-faithful net transform). Source invariant; ROM-free.
    # Fail-on-revert: re-adding any retired shard A/B scale flag reddens this.
    add_test(
        NAME port_glass_shard_scale_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_glass_shard_scale_regression.py"
            --repo-root "${CMAKE_SOURCE_DIR}"
    )
    add_test(
        NAME port_native_sdk_surface_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_native_sdk_surface.py"
            --repo-root "${CMAKE_SOURCE_DIR}"
    )
    add_test(
        NAME port_native_stub_surface_guard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_native_stub_surface.py"
            --repo-root "${CMAKE_SOURCE_DIR}"
    )
    add_test(
        NAME port_public_shell_tool_syntax
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_shell_syntax.py"
            --repo-root "${CMAKE_SOURCE_DIR}"
    )
    # Settings-menu curation table vs. live settings registry.
    add_test(
        NAME settings_menu_model
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_settings_menu_model.py"
            --repo-root "${CMAKE_SOURCE_DIR}"
    )
    # WEB-043: controls-documentation drift gate. Asserts web/index.html's
    # controls surfaces (Controls <dl>, .overlay-note, #overlay-hint) still
    # agree with the engine ground truth in src/platform/input_bindings.c
    # (kDefault/kGpDefault) and src/platform/platform_sdl.c (KEYDOWN handler),
    # and hard-locks the WEB-001 regression: F1/F2/F3-F12 dev keys (gated behind
    # GE007_DEV_HOTKEYS) must never be re-advertised on the page. ROM-free,
    # source-only. Exit 2 = structural parse failure; 1 = a documented drift.
    add_test(
        NAME web_controls_doc_drift
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/web/check_controls_doc.py"
            --repo-root "${CMAKE_SOURCE_DIR}"
    )
    set_tests_properties(web_controls_doc_drift PROPERTIES
        TIMEOUT 60
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    # Headless-Chrome boot smoke for the browser build (the web regression net /
    # Asyncify validation harness). Serves a PREBUILT dist/web on an ephemeral
    # localhost port and drives system Chrome over CDP (--remote-debugging-pipe,
    # zero npm deps) to inject a real ROM, click Play, and assert the WebGPU engine
    # boots to a live, rendering state (canvas visible + stays visible, >=N rAF
    # frames, non-black screenshot, no fatal/Boot-failed, no unexpected console
    # errors). Self-skips (exit 125) when a runtime prerequisite is absent —
    # dist/web, a 12 MB GoldenEye ROM, Chrome, or node — so a CI runner without
    # those SKIPs instead of failing. Timing-fragile by nature (real GPU + browser),
    # so it is tagged LABELS "web": exclude from the default lane with
    # `ctest -LE web`, or run it alone with `ctest -R web_boot_smoke -V`.
    add_test(
        NAME web_boot_smoke
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/web/web_boot_smoke.sh"
    )
    set_tests_properties(web_boot_smoke PROPERTIES
        TIMEOUT 120
        SKIP_RETURN_CODE 125
        LABELS "web"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    # W4.1: browser FRAME-COMPLETENESS lane. Unlike web_boot_smoke (which only
    # asserts a non-black frame — blind to the PERF-005 "sky-flood" bleed), this
    # reaches real gameplay in a level and fails if any single colour floods the
    # viewport. Two passes: deterministic dam, then live + CPU-throttled dam with
    # held movement (the pass that would have caught the bleed). Same
    # self-skip/label posture as web_boot_smoke; longer TIMEOUT for two passes,
    # the second CPU-throttled. See tools/web/webcap.mjs + web_frame_probe.sh.
    add_test(
        NAME web_frame_probe
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/web/web_frame_probe.sh"
    )
    set_tests_properties(web_frame_probe PROPERTIES
        TIMEOUT 180
        SKIP_RETURN_CODE 125
        LABELS "web"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    # Registry honesty: every add_port_validation_smoke(...) below must
    # reference a script that actually ships in tools/, so the advertised
    # validation-lane coverage cannot exceed what a fresh checkout can run.
    add_test(
        NAME port_validation_smoke_registry
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_validation_smoke_registry.py"
            --repo-root "${CMAKE_SOURCE_DIR}"
    )
    # T5: tools/tests/ unit tests —
    # ROM-free, pure Python, never skips. Closes the "CI only py_compiles the
    # tools" gap at the ctest layer for the intro/oracle tooling this plan
    # relies on. Kept in this always-registered block (unlike the intro_*
    # ROM-gated smokes below, which live in their own always-registered but
    # runtime-skippable block near add_port_validation_smoke).
    add_test(
        NAME intro_tools_unittests
        COMMAND python3 -m unittest discover -s "${CMAKE_SOURCE_DIR}/tools/tests" -p "test_*.py"
    )
    set_tests_properties(
        intro_tools_unittests
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 30
    )
    # tools/fidelity/** is export-ignored from the public archive/launch repo
    # (D1); gate its ctest registrations on the tree actually being present.
    if(GE007_HAVE_FIDELITY_PROGRAM)
        # Task 1.3 pixel-oracle calibration (ROM-free): known-good synthetic pair
        # reports 0 unexplained clusters, deliberately-broken pair reports >= 1.
        add_test(
            NAME pixel_diff_unittest
            COMMAND python3 -m unittest discover -s "${CMAKE_SOURCE_DIR}/tools/fidelity/tests" -p "test_pixel_diff.py"
        )
        # Task 2.1 S1 trace-sweep candidate-emission logic (ROM-free).
        add_test(
            NAME sense_trace_candidates_unittest
            COMMAND python3 -m unittest discover -s "${CMAKE_SOURCE_DIR}/tools/fidelity/tests" -p "test_sense_trace_candidates.py"
        )
        # Task 2.1 S1 sweep harness self-test: emits a candidate from a synthetic
        # comparator report with no ROM/ares.
        add_test(
            NAME sense_trace_sweep_selftest
            COMMAND bash "${CMAKE_SOURCE_DIR}/tools/fidelity/sense_trace_sweep.sh" --self-test
        )
        # Task 2.2 S2 pixel-sweep candidate-emission logic (ROM-free): runs the real
        # classifier on the synthetic calibration images.
        add_test(
            NAME sense_pixel_candidates_unittest
            COMMAND python3 -m unittest discover -s "${CMAKE_SOURCE_DIR}/tools/fidelity/tests" -p "test_sense_pixel_candidates.py"
        )
        # Task 2.2 S2 sweep harness self-test: full normalize->diff->classify->
        # candidate pipeline on synthetic images (good->0, broken->>=1), no ROM/ares.
        add_test(
            NAME sense_pixel_sweep_selftest
            COMMAND bash "${CMAKE_SOURCE_DIR}/tools/fidelity/sense_pixel_sweep.sh" --self-test
        )
        set_tests_properties(
            pixel_diff_unittest
            sense_trace_candidates_unittest
            sense_trace_sweep_selftest
            sense_pixel_candidates_unittest
            sense_pixel_sweep_selftest
            PROPERTIES
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                TIMEOUT 30
        )
    endif()
    set_tests_properties(
        port_native_switch_guard
        port_release_ready_guard
        port_public_python_tool_syntax
        port_native_sdk_surface_guard
        port_native_stub_surface_guard
        port_public_shell_tool_syntax
        settings_menu_model
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    if(APPLE)
        add_test(
            NAME macos_app_asset_free_verifier
            COMMAND bash "${CMAKE_SOURCE_DIR}/macos/Tests/test_asset_free_verifier.sh"
        )
        set_tests_properties(
            macos_app_asset_free_verifier
            PROPERTIES
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        )
    endif()

    # Sim-state invariance hash primitive — ROM-free unit test (remaster P0.2 rail).
    add_executable(test_sim_state_hash
        "${CMAKE_SOURCE_DIR}/tests/test_sim_state_hash.c"
        "${CMAKE_SOURCE_DIR}/src/platform/sim_state_hash.c"
    )
    target_include_directories(test_sim_state_hash PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME sim_state_hash COMMAND test_sim_state_hash)

    # PC input tape .ge7tape reader/writer — ROM-free unit test (FID-0034,
    # S-Tier Task 0.6). Compiles only the pure serialisation half of
    # input_tape.c: the record/playback hooks are behind NATIVE_PORT, which the
    # test target does not define, so no joy.h / game headers are pulled in.
    add_executable(test_input_tape
        "${CMAKE_SOURCE_DIR}/tests/test_input_tape.c"
        "${CMAKE_SOURCE_DIR}/src/platform/input_tape.c"
    )
    target_compile_definitions(test_input_tape PRIVATE INPUT_TAPE_UNIT_TEST)
    target_include_directories(test_input_tape PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME input_tape COMMAND test_input_tape)

    # Smooth env-normals builder — ROM-free unit test (remaster W1.E1.T1).
    # Compiles only the pure builder core (game-facing wrapper excluded via the
    # GFX_ROOM_NORMALS_UNIT_TEST guard, so no g_BgRoomInfo dependency).
    add_executable(test_room_normals
        "${CMAKE_SOURCE_DIR}/tests/test_room_normals.c"
        "${CMAKE_SOURCE_DIR}/src/platform/fast3d/gfx_room_normals.c"
    )
    target_compile_definitions(test_room_normals PRIVATE GFX_ROOM_NORMALS_UNIT_TEST)
    target_include_directories(test_room_normals PRIVATE
        "${CMAKE_SOURCE_DIR}/src/platform/fast3d")
    if(NOT WIN32)
        # glibc keeps libm separate; needed for lround() (no-op on macOS).
        target_link_libraries(test_room_normals m)
    endif()
    add_test(NAME room_normals COMMAND test_room_normals)

    # N64 struct-layout lock — ROM-free compile-time guard (S-tier Task 0.7,
    # FID-0035 + FID-0036 first half). The _Static_asserts in the TU fire at
    # COMPILE time if any asserted sizeof/offsetof of a StandTile navmesh or
    # PROPDEF record struct drifts, failing the build instead of corrupting
    # memory at runtime — the exact v0.3.2 Windows-crash class (MinGW's default
    # -mms-bitfields repacked StandTile and OOB'd the stan navmesh). Built with
    # the SAME layout-critical flags (-fms-extensions to parse the `inherits`
    # idiom; -mno-ms-bitfields to force SysV bit-field packing) + defines +
    # include dirs as the ge007 target so it locks the real game layout. The
    # global add_definitions() (CMakeLists:536) runs after this block, so the
    # game defines are applied explicitly here.
    add_executable(test_struct_layout
        "${CMAKE_SOURCE_DIR}/tests/test_struct_layout.c"
    )
    target_compile_definitions(test_struct_layout PRIVATE
        NONMATCHING NATIVE_PORT PORT_FIXME_STUBS _LANGUAGE_C
        VERSION_US LANG_US REFRESH_NTSC LEFTOVERDEBUG
        LEFTOVERSPECTRUM BUGFIX_R0 BYTEMATCH SDL_MAIN_HANDLED)
    target_compile_options(test_struct_layout PRIVATE
        -w -fms-extensions -mno-ms-bitfields)
    target_include_directories(test_struct_layout PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/include/PR"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/game"
        "${CMAKE_SOURCE_DIR}/src/platform"
        "${CMAKE_SOURCE_DIR}/src/platform/fast3d"
        "${CMAKE_SOURCE_DIR}/src/libultra/audio"
        "${CMAKE_SOURCE_DIR}/assets"
        "${CMAKE_SOURCE_DIR}/lib/glad/include"
        "${CMAKE_SOURCE_DIR}/lib/stb"
        "${CMAKE_SOURCE_DIR}")
    add_test(NAME struct_layout COMMAND test_struct_layout)

    # MP other-player beam-tick raw-offset divergence — ROM-free unit test
    # (FID-0094). Compiles the real struct player / struct hand (bondview.h,
    # SAME layout-critical flags/defines/includes as test_struct_layout) and
    # asserts at runtime that the faithful named-field offsets playerTickBeams
    # uses (hands[i].weapon_firing_status; hands[i].field_B50/B54/B58) never
    # coincide with the legacy raw N64 offsets (0x875/0xC1D; i*936+0xB50).
    add_executable(test_mp_beam_rawcast
        "${CMAKE_SOURCE_DIR}/tests/test_mp_beam_rawcast.c"
        "${CMAKE_SOURCE_DIR}/src/platform/mp_beam_rawcast.c"
    )
    target_compile_definitions(test_mp_beam_rawcast PRIVATE
        NONMATCHING NATIVE_PORT PORT_FIXME_STUBS _LANGUAGE_C
        VERSION_US LANG_US REFRESH_NTSC LEFTOVERDEBUG
        LEFTOVERSPECTRUM BUGFIX_R0 BYTEMATCH SDL_MAIN_HANDLED)
    target_compile_options(test_mp_beam_rawcast PRIVATE
        -w -fms-extensions -mno-ms-bitfields)
    target_include_directories(test_mp_beam_rawcast PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/include/PR"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/game"
        "${CMAKE_SOURCE_DIR}/src/platform"
        "${CMAKE_SOURCE_DIR}/src/platform/fast3d"
        "${CMAKE_SOURCE_DIR}/src/libultra/audio"
        "${CMAKE_SOURCE_DIR}/assets"
        "${CMAKE_SOURCE_DIR}/lib/glad/include"
        "${CMAKE_SOURCE_DIR}/lib/stb"
        "${CMAKE_SOURCE_DIR}")
    add_test(NAME mp_beam_rawcast COMMAND test_mp_beam_rawcast)

    # FP-weapon perspnorm value/order fix — ROM-free unit test (FID-0077):
    # retail near arg 0.0f => perspnorm 436, pre-fix port 1.0f => 435.
    add_executable(test_fp_weapon_perspnorm
        "${CMAKE_SOURCE_DIR}/tests/test_fp_weapon_perspnorm.c"
        "${CMAKE_SOURCE_DIR}/src/platform/fp_weapon_perspnorm.c"
    )
    target_include_directories(test_fp_weapon_perspnorm PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME fp_weapon_perspnorm COMMAND test_fp_weapon_perspnorm)

    # Watch ammo-panel weapon-switch early-out — ROM-free unit test (FID-0084):
    # retail hides the panel for hand states 6/7; pre-fix port drew through it.
    add_executable(test_watch_ammo_switchstate
        "${CMAKE_SOURCE_DIR}/tests/test_watch_ammo_switchstate.c"
        "${CMAKE_SOURCE_DIR}/src/platform/watch_ammo_switchstate.c"
    )
    target_include_directories(test_watch_ammo_switchstate PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME watch_ammo_switchstate COMMAND test_watch_ammo_switchstate)

    # Registering GE007_* env-flag accessors — ROM-free unit test.
    add_executable(test_port_env
        "${CMAKE_SOURCE_DIR}/tests/test_port_env.c"
        "${CMAKE_SOURCE_DIR}/src/platform/port_env.c"
    )
    target_include_directories(test_port_env PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME port_env COMMAND test_port_env)

    # Movement-stick radial deadzone — ROM-free unit test (FID-0015 / M2.1).
    # Compiles only the pure radial_deadzone.c TU (no SDL / game headers): the
    # shared platformApplyRadialDeadzone helper + the movement-stick N64 map.
    add_executable(test_radial_deadzone
        "${CMAKE_SOURCE_DIR}/tests/test_radial_deadzone.c"
        "${CMAKE_SOURCE_DIR}/src/platform/radial_deadzone.c"
    )
    target_include_directories(test_radial_deadzone PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    if(NOT WIN32)
        target_link_libraries(test_radial_deadzone m)   # sqrtf (glibc keeps libm separate)
    endif()
    add_test(NAME radial_deadzone COMMAND test_radial_deadzone)

    # Stick MAGNITUDE / range verify-ratchet — ROM-free unit test (FID-0060):
    # full SDL deflection maps to the N64 practical ±80 (GE's /70 analog tuning
    # saturates at raw ≈75), NOT a hot ±127-equivalent. Shares the exact pure
    # helper the runtime uses (radial_deadzone.c). Composes with FID-0015 geometry.
    add_executable(test_stick_range
        "${CMAKE_SOURCE_DIR}/tests/test_stick_range.c"
        "${CMAKE_SOURCE_DIR}/src/platform/radial_deadzone.c"
    )
    target_include_directories(test_stick_range PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    if(NOT WIN32)
        target_link_libraries(test_stick_range m)   # sqrtf (glibc keeps libm separate)
    endif()
    add_test(NAME stick_range COMMAND test_stick_range)

    # Mouse-wheel weapon-cycle queue/drain — ROM-free unit test (FID-0016 / M2.2).
    add_executable(test_weapon_cycle_queue
        "${CMAKE_SOURCE_DIR}/tests/test_weapon_cycle_queue.c"
        "${CMAKE_SOURCE_DIR}/src/platform/weapon_cycle_queue.c"
    )
    target_include_directories(test_weapon_cycle_queue PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME weapon_cycle_queue COMMAND test_weapon_cycle_queue)

    # Automatic Shotgun (AUTOSHOT) firing-dispatch grouping — ROM-free unit test
    # (FID-0052): jpt_weapon_bullet_type routes AUTOSHOT to shotgun_mine, not pistol.
    add_executable(test_weapon_bullet_type
        "${CMAKE_SOURCE_DIR}/tests/test_weapon_bullet_type.c"
        "${CMAKE_SOURCE_DIR}/src/platform/weapon_bullet_type.c"
    )
    target_include_directories(test_weapon_bullet_type PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME weapon_bullet_type COMMAND test_weapon_bullet_type)

    # Weapon equip/reload SFX cue tables — ROM-free unit test (AUDIT-0028): the
    # resolver must reproduce retail jpt_80054194 (equip) / jpt_80054294 (reload)
    # entry-for-entry, including the < ITEM_BLACKBOX(62) bound (so GOLDENEYEKEY(61)
    # is table-silent, not the audible else-branch) and the != ITEM_TOKEN post-table
    # gun cue. The test re-encodes both tables independently so a co-edited array
    # can't mask a divergence.
    add_executable(test_weapon_action_sfx
        "${CMAKE_SOURCE_DIR}/tests/test_weapon_action_sfx.c"
        "${CMAKE_SOURCE_DIR}/src/platform/weapon_action_sfx.c"
    )
    target_include_directories(test_weapon_action_sfx PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME weapon_action_sfx COMMAND test_weapon_action_sfx)

    # Audio CUSTOM-FX param table shape/values — ROM-free unit test (audit
    # backlog W3.5 review recommendation): links the real audi_port.c and
    # reads s_portAudioCustomFxParams through a test-only accessor so a
    # silent edit to the table's shape or interior constants is caught.
    # audi_port.c's audio-thread/DMA/mixer externs are link-only stubbed in
    # test_audio_fx_params_stubs.c (never called by this test).
    add_executable(test_audio_fx_params
        "${CMAKE_SOURCE_DIR}/tests/test_audio_fx_params.c"
        "${CMAKE_SOURCE_DIR}/tests/test_audio_fx_params_stubs.c"
        "${CMAKE_SOURCE_DIR}/src/platform/audi_port.c"
        "${CMAKE_SOURCE_DIR}/src/platform/audio_queue_controller.c"
    )
    # The global add_definitions() (CMakeLists:1578-ish) runs after this
    # block, so NATIVE_PORT (audi_port.c's compile gate) is applied
    # explicitly here, mirroring test_struct_layout above.
    target_compile_definitions(test_audio_fx_params PRIVATE
        NONMATCHING NATIVE_PORT PORT_FIXME_STUBS _LANGUAGE_C
        VERSION_US LANG_US REFRESH_NTSC LEFTOVERDEBUG
        LEFTOVERSPECTRUM BUGFIX_R0 BYTEMATCH SDL_MAIN_HANDLED)
    target_include_directories(test_audio_fx_params PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/include/PR"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/platform"
    )
    add_test(NAME audio_fx_params COMMAND test_audio_fx_params)

    # FID-0141: the live audio occupancy controller measures its actual pump
    # cadence so weak machines retain the configured queue cushion below 40 fps.
    # Pure ROM-free model: pins 60/40/30 fps convergence, max clamping, recovery,
    # and a fixed-60-base negative control that reproduces the thin cushion.
    add_executable(test_audio_queue_controller
        "${CMAKE_SOURCE_DIR}/tests/test_audio_queue_controller.c"
        "${CMAKE_SOURCE_DIR}/tests/test_audio_fx_params_stubs.c"
        "${CMAKE_SOURCE_DIR}/src/platform/audio_queue_controller.c"
        "${CMAKE_SOURCE_DIR}/src/platform/audi_port.c"
    )
    target_compile_definitions(test_audio_queue_controller PRIVATE
        NONMATCHING NATIVE_PORT PORT_FIXME_STUBS _LANGUAGE_C
        VERSION_US LANG_US REFRESH_NTSC LEFTOVERDEBUG
        LEFTOVERSPECTRUM BUGFIX_R0 BYTEMATCH SDL_MAIN_HANDLED
        PORT_AUDIO_CONTROLLER_TEST_HOOKS)
    target_include_directories(test_audio_queue_controller PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/include/PR"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/platform"
    )
    add_test(NAME audio_queue_controller COMMAND test_audio_queue_controller)

    # Sticky-projectile deposit-motion endpoint pull-back — ROM-free unit test
    # (FID-0065): retail clamps the collision endpoint + impact to bg_hit-0.1*travel
    # ONLY on a wall hit (result==0), measured from arg2; the port inverted the
    # guard and read the base from dest. Fails if guard polarity or base operand reverts.
    add_executable(test_projectile_endpoint_clamp
        "${CMAKE_SOURCE_DIR}/tests/test_projectile_endpoint_clamp.c"
        "${CMAKE_SOURCE_DIR}/src/platform/projectile_endpoint_clamp.c"
    )
    target_include_directories(test_projectile_endpoint_clamp PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME projectile_endpoint_clamp COMMAND test_projectile_endpoint_clamp)

    # Auto-aim target divisor + score — ROM-free unit test (AUDIT-0008): the pure
    # helpers factored out of sub_GAME_7F03D188 so the width divisor is always
    # defined (the vertical-only path no longer divides by an uninitialized stack
    # slot) while the autoaim_x-on divisor/score arithmetic stays byte-identical
    # to retail. Fails if the divisor operand order or the three-branch score
    # formula drifts.
    add_executable(test_autoaim_score
        "${CMAKE_SOURCE_DIR}/tests/test_autoaim_score.c"
        "${CMAKE_SOURCE_DIR}/src/platform/autoaim_score.c"
    )
    target_include_directories(test_autoaim_score PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME autoaim_score COMMAND test_autoaim_score)

    # sub_GAME_7F0AF20C roomset filter — ROM-free unit test (FID-0079): retail
    # reads the roomset as u8 room bytes (0xFF-terminated, <=4 entries; ASM
    # src/game/stan.c:1063-1086), the port cast to s32* and never matched, so
    # flag-8 projectile tile reacquire (chrobjhandler.c:5328) always failed.
    # Compiles only the pure stan_roomset.c TU (both readers + the dispatcher the
    # real reader calls). Fails if the byte reader reverts to s32, the legacy
    # negative control reverts to bytes, or the dispatcher wiring flips.
    add_executable(test_stan_roomset
        "${CMAKE_SOURCE_DIR}/tests/test_stan_roomset.c"
        "${CMAKE_SOURCE_DIR}/src/platform/stan_roomset.c"
    )
    target_include_directories(test_stan_roomset PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME stan_roomset COMMAND test_stan_roomset)

    # Low-32 GBI pointer registry — ROM-free unit test (AUDIT-0009): the
    # open-addressed table must never silently evict a live mapping (whole-table
    # lossless insert), must tombstone (not zero) on range-invalidation so a
    # mid-chain delete never hides a later live mapping, and must fail closed on
    # an ambiguous same-low-32 collision. Header-only static-inline logic, so the
    # test owns the table + telemetry definitions and links no engine TU.
    add_executable(test_gfx_ptr_registry
        "${CMAKE_SOURCE_DIR}/tests/test_gfx_ptr_registry.c"
    )
    target_include_directories(test_gfx_ptr_registry PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME gfx_ptr_registry COMMAND test_gfx_ptr_registry)

    # Duplicate gamepad-binding conflict policy — ROM-free/SDL-free unit test
    # (AUDIT-0050): the shared ownership primitive bindingOwnerOf() that the
    # Controls UI move-or-reject path routes through, covering BUTTON and TRIGGER
    # encodings alike (the trigger capture branch previously skipped the owner
    # check and silently double-bound). Compiles only the pure binding_conflict.c TU.
    add_executable(test_binding_conflict
        "${CMAKE_SOURCE_DIR}/tests/test_binding_conflict.c"
        "${CMAKE_SOURCE_DIR}/src/platform/binding_conflict.c"
    )
    target_include_directories(test_binding_conflict PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME binding_conflict COMMAND test_binding_conflict)

    # Gamepad menu-button cross-domain conflict — ROM-free unit test (AUDIT-0025):
    # the overlay opens on the CONFIGURED Input.MenuToggleButton, so gpValid must
    # reserve that button (not a hardcoded Back) and a menu-wins reconciliation
    # must clear any gameplay binding (including the stock Jump=A default) that
    # equals it. Compiles only the pure gp_reserved.c predicates (no SDL/config).
    add_executable(test_gamepad_menu_conflict
        "${CMAKE_SOURCE_DIR}/tests/test_gamepad_menu_conflict.c"
        "${CMAKE_SOURCE_DIR}/src/platform/gp_reserved.c"
    )
    target_include_directories(test_gamepad_menu_conflict PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME gamepad_menu_conflict COMMAND test_gamepad_menu_conflict)

    # Screenshot-series cadence + write contract — ROM-free/GPU-free unit test
    # (AUDIT-0003 remainder): the AFTER_FRAME/EVERY/LIMIT/room accounting and the
    # P6 write (filename pattern, bottom-to-top row flip, write-only-on-durable-
    # file contract, written counter) were extracted from a static function in
    # the 25k-line gfx_pc.c into screenshot_series.c, which now takes a readback
    # function pointer. A mock readback (deterministic pixels; controllable
    # success/failure) exercises cadence, room filter, flip/size, and failure
    # handling with no GL/Metal/WebGPU context. Compiles only the pure
    # screenshot_series.c TU.
    add_executable(test_screenshot_series
        "${CMAKE_SOURCE_DIR}/tests/test_screenshot_series.c"
        "${CMAKE_SOURCE_DIR}/src/platform/fast3d/screenshot_series.c"
    )
    target_include_directories(test_screenshot_series PRIVATE "${CMAKE_SOURCE_DIR}/src/platform/fast3d")
    add_test(NAME screenshot_series COMMAND test_screenshot_series)

    # System-hotkey predicates + gamepad button-name reader — ROM-free/SDL-free
    # unit test (AUDIT-0024 core): sysKeyValid bounds a keycode to SDL's keycode
    # range, sysKeyMutualConflict enforces menu-toggle != fps-toggle (both are
    # keycodes), and gamepadButtonName is the bounds-checked reader over the
    # button-name table (mirrored in sys_hotkey.c to stay off <SDL.h>). Compiles
    # only the pure sys_hotkey.c TU. Cross-namespace keyboard conflict needs
    # SDL_GetScancodeFromKey and is intentionally left out of this pure core.
    add_executable(test_sys_hotkey
        "${CMAKE_SOURCE_DIR}/tests/test_sys_hotkey.c"
        "${CMAKE_SOURCE_DIR}/src/platform/sys_hotkey.c"
    )
    target_include_directories(test_sys_hotkey PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME sys_hotkey COMMAND test_sys_hotkey)

    # Settings save-status chooser — ROM-free/SDL-free/ImGui-free unit test
    # (AUDIT-0036 pure slice): saveStatusMessage maps the MgbConfigSaveResult
    # tri-state (OK / SUPPRESSED / FAILED) to the exact status line surfaced under
    # Apply/Save, and saveStatusIsFailure flags ONLY FAILED (so an intentional
    # faithful-mode SUPPRESSED no-write is not mistaken for an error and does not
    # trigger Retry / keep-overlay-open). Compiles only the pure save_status.c TU;
    # depends solely on the engine-free MgbConfigSaveResult enum in
    # src/app/config_schema.h.
    add_executable(test_save_status
        "${CMAKE_SOURCE_DIR}/tests/test_save_status.c"
        "${CMAKE_SOURCE_DIR}/src/platform/save_status.c"
    )
    target_include_directories(test_save_status PRIVATE
        "${CMAKE_SOURCE_DIR}/src/platform"
        "${CMAKE_SOURCE_DIR}/src/app")
    add_test(NAME save_status COMMAND test_save_status)

    # maybe_detonate_object gate/armour/salvage helpers — ROM-free unit test
    # (FID-0074): retail ASM src/game/chrobjhandler.c:38029-38459. Guards all
    # five divergences on both flag sides (D2 armour polarity, D3 unarmed-gate
    # polarity, D4 armed INVINCIBLE early-out, D5 type-7/8 non-explosive return,
    # D1 salvage slot arithmetic). Fails if any polarity reverts or the legacy
    # negative control (GE007_NO_DETONATE_OBJECT_FIX) drifts from the old port.
    add_executable(test_chrobj_detonate
        "${CMAKE_SOURCE_DIR}/tests/test_chrobj_detonate.c"
        "${CMAKE_SOURCE_DIR}/src/platform/chrobj_detonate.c"
    )
    target_include_directories(test_chrobj_detonate PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME chrobj_detonate COMMAND test_chrobj_detonate)

    # Object-hit bullet-impact suppression gate — ROM-free unit test (FID-0069):
    # retail sub_GAME_7F04EA68 skips the impact block (+ its randomGetNext draws)
    # for ITEM_WATCHLASER (23); the pre-fix port gated on ITEM_TASER (31). Fails if
    # the gate constant reverts or GE007_NO_WATCHLASER_IMPACT_FIX drifts.
    add_executable(test_chrobj_impact_suppress
        "${CMAKE_SOURCE_DIR}/tests/test_chrobj_impact_suppress.c"
        "${CMAKE_SOURCE_DIR}/src/platform/chrobj_impact_suppress.c"
    )
    target_include_directories(test_chrobj_impact_suppress PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME chrobj_impact_suppress COMMAND test_chrobj_impact_suppress)

    # Watch-menu hand-item exit lifecycle — ROM-free unit test (FID-0073, reused
    # by FID-0072): retail sets the hand item ONCE and never restores it; the
    # pre-fix port did a phantom per-frame save/restore. Fails if the phantom
    # restore is reintroduced (faithful==legacy) or the negative controls drift.
    add_executable(test_watchmenu_hand_lifecycle
        "${CMAKE_SOURCE_DIR}/tests/test_watchmenu_hand_lifecycle.c"
        "${CMAKE_SOURCE_DIR}/src/platform/watchmenu_hand_lifecycle.c"
    )
    target_include_directories(test_watchmenu_hand_lifecycle PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME watchmenu_hand_lifecycle COMMAND test_watchmenu_hand_lifecycle)

    # Watch controller-settings page per-value fixes — ROM-free unit test
    # (FID-0072): D1 the face-button depress −10 offset lands on posN.y (arg0,
    # table copy) not rotN.y (arg4, vertices); D2 the page drives hand 0
    # (GUNRIGHT) not GUNLEFT. Fails if either per-value fix reverts or the
    # GE007_NO_WATCH_JOYPAD_FIX negative control drifts.
    add_executable(test_watch_joypad_page
        "${CMAKE_SOURCE_DIR}/tests/test_watch_joypad_page.c"
        "${CMAKE_SOURCE_DIR}/src/platform/watch_joypad_page.c"
    )
    target_include_directories(test_watch_joypad_page PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME watch_joypad_page COMMAND test_watch_joypad_page)

    # Full-auto fire-cadence tick-scaling — ROM-free unit test (FID-0056):
    # Input.FireRateAuthentic scales the automatic gate to the N64 per-frame
    # cadence. Fails if the divisor scaling or the counter tick-scaling reverts.
    add_executable(test_fire_rate_authentic
        "${CMAKE_SOURCE_DIR}/tests/test_fire_rate_authentic.c"
        "${CMAKE_SOURCE_DIR}/src/platform/fire_rate_authentic.c"
    )
    target_include_directories(test_fire_rate_authentic PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME fire_rate_authentic COMMAND test_fire_rate_authentic)

    # Metal MSAA sample-count resolution — ROM-free, Metal-free unit test
    # (FID-0018). Compiles the exact pure helper the Metal backend uses
    # (gfx_msaa_util.c) so the requested->device-clamped rasterSampleCount ladder
    # and the same-pass sample-count consistency guard are checked without a live
    # MTLDevice. Runs on every platform (the helper is pure C).
    add_executable(test_msaa_sample_count
        "${CMAKE_SOURCE_DIR}/tests/test_msaa_sample_count.c"
        "${CMAKE_SOURCE_DIR}/src/platform/fast3d/gfx_msaa_util.c"
    )
    target_include_directories(test_msaa_sample_count PRIVATE "${CMAKE_SOURCE_DIR}/src/platform/fast3d")
    add_test(NAME msaa_sample_count COMMAND test_msaa_sample_count)

    # speedgraphframes frame-spike clamp — ROM-free unit test (FID-0017 / M2.4).
    add_executable(test_speedgraph_clamp
        "${CMAKE_SOURCE_DIR}/tests/test_speedgraph_clamp.c"
        "${CMAKE_SOURCE_DIR}/src/platform/frame_clamp.c"
    )
    target_include_directories(test_speedgraph_clamp PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME speedgraph_clamp COMMAND test_speedgraph_clamp)

    # Watch inventory UP snap-scroll gate — ROM-free unit test (FID-0100):
    # retail snaps up on button OR stick-full-up (||, matching the down sibling);
    # the port defect required button AND stick (&&). Fails if the gate reverts
    # to && or the GE007_NO_WATCH_UPSCROLL_FIX legacy path flips.
    add_executable(test_watch_scroll_gate
        "${CMAKE_SOURCE_DIR}/tests/test_watch_scroll_gate.c"
        "${CMAKE_SOURCE_DIR}/src/platform/watch_scroll_gate.c"
    )
    target_include_directories(test_watch_scroll_gate PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME watch_scroll_gate COMMAND test_watch_scroll_gate)

    # Watch inventory guPerspective aspect — ROM-free unit test (FID-0098):
    # retail passes 0x3FAAAAAB (4/3); the port defect passed 0x3FA49249 (9/7).
    # Asserts both IEEE-754 bit patterns so a revert of either reddens.
    add_executable(test_watch_inv_aspect
        "${CMAKE_SOURCE_DIR}/tests/test_watch_inv_aspect.c"
        "${CMAKE_SOURCE_DIR}/src/platform/watch_inv_aspect.c"
    )
    target_include_directories(test_watch_inv_aspect PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME watch_inv_aspect COMMAND test_watch_inv_aspect)

    # Aim-bone arg0 validity dispatch — ROM-free unit test (FID-0101):
    # sub_GAME_7F02083C proceeds for arg0 in {0,1,2,3} (retail `bnezl arg0`); the
    # port defect returned for arg0==0, deadening the gun-hand aim-bone block.
    # Fails if arg0==0 stops proceeding or GE007_NO_GUNHAND_AIMBONE_FIX flips.
    add_executable(test_aimbone_dispatch
        "${CMAKE_SOURCE_DIR}/tests/test_aimbone_dispatch.c"
        "${CMAKE_SOURCE_DIR}/src/platform/aimbone_dispatch.c"
    )
    target_include_directories(test_aimbone_dispatch PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME aimbone_dispatch COMMAND test_aimbone_dispatch)

    # Bbox-hull vertex-count clamp — ROM-free unit test (FID-0096): the port
    # clamps to 4 by default (memory-safe for all callers incl. the tank);
    # GE007_HULL_VERTS_RETAIL opts into the retail true count (no clamp). Fails if
    # the default clamp is dropped or the opt-in re-clamps.
    add_executable(test_hull_vertex_clamp
        "${CMAKE_SOURCE_DIR}/tests/test_hull_vertex_clamp.c"
        "${CMAKE_SOURCE_DIR}/src/platform/hull_vertex_clamp.c"
    )
    target_include_directories(test_hull_vertex_clamp PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME hull_vertex_clamp COMMAND test_hull_vertex_clamp)

    # Collision-hull builder corpus — ROM-free regression (FID-0132). Drives the
    # exact production geometry (src/game/chrprop_hull_impl.inc) over the Dam desk
    # boxes, axis-aligned ties, rotated boxes and a fixed-seed randomized batch,
    # asserting >=4 distinct corners for boxes with 2D area and every projected
    # corner inside-or-on the hull. Default (unclamped, the fix) PASSES;
    # HULL_TEST_CLAMP=1 reproduces the pre-fix clamped RED.
    add_executable(test_hull_builder
        "${CMAKE_SOURCE_DIR}/tests/test_hull_builder.c"
        "${CMAKE_SOURCE_DIR}/src/platform/hull_vertex_clamp.c"
    )
    target_include_directories(test_hull_builder PRIVATE
        "${CMAKE_SOURCE_DIR}/src/platform"
        "${CMAKE_SOURCE_DIR}/src/game")
    add_test(NAME hull_builder COMMAND test_hull_builder)

    # Background-only bullet-impact spawn gate — ROM-free unit test (FID-0097):
    # the port keeps a texture_index>=0 guard around the retail g_Textures[-1] OOB
    # read as the memory-safe default; GE007_BG_IMPACT_RETAIL_OOB opts into
    # spawning for index<0 without reading OOB. Fails if the default guard drops
    # or the water/snow skip is lost.
    add_executable(test_bg_impact_guard
        "${CMAKE_SOURCE_DIR}/tests/test_bg_impact_guard.c"
        "${CMAKE_SOURCE_DIR}/src/platform/bg_impact_guard.c"
    )
    target_include_directories(test_bg_impact_guard PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME bg_impact_guard COMMAND test_bg_impact_guard)

    # HUD ammo-icon rect-center anchor + mirror — ROM-free unit test (FID-0067):
    # retail centers the icon at y - h*0.5 (y>=0) / H + S - frac(h) (y<0) and
    # never mirrors the left dual-wield icon; the port defect drew it floor(h/2)
    # px off on both branches and mirror-imaged the left icon. Pins the fixed and
    # legacy values so a revert of either or a GE007_NO_AMMO_ICON_ANCHOR_FIX flip
    # reddens.
    add_executable(test_ammo_icon_anchor
        "${CMAKE_SOURCE_DIR}/tests/test_ammo_icon_anchor.c"
        "${CMAKE_SOURCE_DIR}/src/platform/ammo_icon_anchor.c"
    )
    target_include_directories(test_ammo_icon_anchor PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME ammo_icon_anchor COMMAND test_ammo_icon_anchor)

    # Paused-watch scene renderer constants — ROM-free unit test (FID-0068):
    # retail passes guPerspective aspect 0x3FBA2E8C (16/11) and stores the room
    # tint into envcolour big-endian packed (r<<24|g<<16|b<<8|a, or 205 for
    # states 5/12); the port passed 1.456f (0x3FBA5E35) and an LE-reversed
    # *(u32*)&tileColor into the inert cullmode. Pins both so a revert or a
    # GE007_NO_WATCH_RENDER_FIX flip reddens.
    add_executable(test_watch_scene_render
        "${CMAKE_SOURCE_DIR}/tests/test_watch_scene_render.c"
        "${CMAKE_SOURCE_DIR}/src/platform/watch_scene_render.c"
    )
    target_include_directories(test_watch_scene_render PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME watch_scene_render COMMAND test_watch_scene_render)

    # MP health/armour-bar draw gate — ROM-free unit test (FID-0070): retail
    # gates on HealthShowTime && watch-idle only (both ASM variants); the port
    # ORed in DamageShowTime, drawing the bars during damage flashes and skipping
    # the healthdisplaytime decrement. Pins the truth tables (esp. the divergent
    # damage-flash case) so a revert or a GE007_NO_MP_HEALTHBAR_DAMAGE_GATE_FIX
    # flip reddens.
    add_executable(test_mp_healthbar_gate
        "${CMAKE_SOURCE_DIR}/tests/test_mp_healthbar_gate.c"
        "${CMAKE_SOURCE_DIR}/src/platform/mp_healthbar_gate.c"
    )
    target_include_directories(test_mp_healthbar_gate PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME mp_healthbar_gate COMMAND test_mp_healthbar_gate)

    # MP object-respawn tail — ROM-free unit test (FID-0103): retail's respawn
    # tail (US ASM 7F03C8AC-7F03C8E4) plays sound 82 + posts a sound event and,
    # for PROPDEF_ARMOUR (type 21), resets armour amount to initialamount; the
    # port dropped the whole tail so regenerating objects respawned silently.
    # Pins the retail vs legacy decision so a revert or a
    # GE007_NO_MP_RESPAWN_TAIL_FIX flip reddens.
    add_executable(test_mp_respawn_tail
        "${CMAKE_SOURCE_DIR}/tests/test_mp_respawn_tail.c"
        "${CMAKE_SOURCE_DIR}/src/platform/mp_respawn_tail.c"
    )
    target_include_directories(test_mp_respawn_tail PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME mp_respawn_tail COMMAND test_mp_respawn_tail)

    # Glass shot-depth acceptance — ROM-free unit test (FID-0083). Retail hit-depth
    # gates sub_GAME_7F04E720 / sub_GAME_7F04E9BC reject any object hit behind the
    # limit plane; the port's glass-crack acceptance window is now OPT-IN with a
    # faithful default of 0.0 (retail-exact). Pins both sides so a revert of the
    # default to a positive tolerance, or a change to the window math, reddens.
    add_executable(test_glass_shot_depth
        "${CMAKE_SOURCE_DIR}/tests/test_glass_shot_depth.c"
        "${CMAKE_SOURCE_DIR}/src/platform/glass_shot_depth.c"
    )
    target_include_directories(test_glass_shot_depth PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME glass_shot_depth COMMAND test_glass_shot_depth)

    # IA16/RGBA16 TLUT decode — ROM-free unit test (DAM_PARITY_DEEP_DIVE
    # 2026-07-17 §4.7): pins N64 IA16 byte order (intensity HIGH byte, alpha
    # LOW byte) in the run-dl CI import decode. The shipped decoder was
    # byte-swapped, so opaque-dark IA16 palette entries decoded white-with-
    # alpha-0 and IA16-palette CI textures (Dam monitor text screens) rendered
    # as flat vertex shade. A revert of the byte order reddens this.
    add_executable(test_palette_decode
        "${CMAKE_SOURCE_DIR}/tests/test_palette_decode.c"
    )
    add_test(NAME palette_decode COMMAND test_palette_decode)

    # Spectrum easter-egg SETTILE tile field — ROM-free unit test (FID-0107):
    # spectrum_draw_screen must emit SETTILE w1 = 0x07000000 (tile 7 = G_TX_LOADTILE)
    # so both SETTILEs configure the tile their LOADTLUT/LOADBLOCK load into. The
    # port transcribed 0x00070000 (tile 0 + stray cmt/maskt), a digit shift.
    # Pins the corrected constant so a revert or a GE007_NO_SPECTRUM_LOADTILE_FIX
    # flip reddens.
    add_executable(test_spectrum_settile
        "${CMAKE_SOURCE_DIR}/tests/test_spectrum_settile.c"
        "${CMAKE_SOURCE_DIR}/src/platform/spectrum_settile.c"
    )
    target_include_directories(test_spectrum_settile PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME spectrum_settile COMMAND test_spectrum_settile)

    # G_SETTEX texture-cache key discriminator — ROM-free unit test (TMEM-2,
    # DAM_RENDER_DEEP_DIVE 2026-07-18): the settex cache was keyed on the texture
    # number alone, so a CI texture re-drawn against a different TLUT returned the
    # first palette's decode. gfx_settex_cache_key_hit() (the header gfx_pc.c uses
    # in the cache lookup) requires CI entries to match the live palette identity;
    # non-CI entries ignore it and stay byte-identical. A revert of the palette
    # component reddens the changed-palette / grayscale-fallback cases.
    add_executable(test_settex_cache_key
        "${CMAKE_SOURCE_DIR}/tests/test_settex_cache_key.c"
    )
    target_include_directories(test_settex_cache_key PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME settex_cache_key COMMAND test_settex_cache_key)

    # Setup padnames/boundpadnames re-layout — ROM-free unit test (FID-0037):
    # the setup file's padnames (header word 8) / boundpadnames (word 9) tables are
    # big-endian 32-bit file-local offset arrays, zero-terminated, each pointing at
    # a pad-name string. The port left both NULL (prop.c:2538); retail resolves +
    # relocates them. Pins the byte-swap, terminator, base+offset relocation and the
    # legacy negative control (GE007_NO_PADNAMES_FIX -> table NULL).
    add_executable(test_setup_pnames
        "${CMAKE_SOURCE_DIR}/tests/test_setup_pnames.c"
        "${CMAKE_SOURCE_DIR}/src/platform/setup_pnames.c"
    )
    target_include_directories(test_setup_pnames PRIVATE "${CMAKE_SOURCE_DIR}/src/platform")
    add_test(NAME setup_pnames COMMAND test_setup_pnames)

    # RSP texgen (environment mapping) coordinate mapping. There was no coverage
    # of this at all, which is how the 2x-scale defect survived an audit that had
    # already spotted it.
    add_executable(test_texgen_coords
        "${CMAKE_SOURCE_DIR}/tests/test_texgen_coords.c"
    )
    target_include_directories(test_texgen_coords PRIVATE "${CMAKE_SOURCE_DIR}/src/platform/fast3d")
    if(NOT MSVC)
        target_link_libraries(test_texgen_coords PRIVATE m)
    endif()
    add_test(NAME texgen_coords COMMAND test_texgen_coords)

    # Generated GE007_* flag reference must match the source flag surface, so
    # docs/ENV_FLAGS.md cannot drift from the code (regenerate with
    # tools/gen_env_reference.py --out docs/ENV_FLAGS.md).
    add_test(
        NAME env_reference_current
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/gen_env_reference.py"
            --repo-root "${CMAKE_SOURCE_DIR}"
            --check docs/ENV_FLAGS.md
    )
    set_tests_properties(env_reference_current PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # tools/fidelity/ + docs/fidelity/ are export-ignored from the public
    # archive/launch repo (D1); a checkout without that tree just doesn't
    # register these ctests.
    if(GE007_HAVE_FIDELITY_PROGRAM)
        # Fidelity findings ledger must stay schema-valid with consistent transition
        # history (S-tier program Phase 0, Task 0.2). ROM-free python guard — the
        # loop's persistent memory can never silently corrupt.
        add_test(
            NAME fidelity_ledger_valid
            COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/fidelity/ledger.py" validate
        )
        set_tests_properties(fidelity_ledger_valid PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

        # Ledger transition edge-set + evidence-path validation (C5, 2026-07-10 review):
        # `transition` must reject skip-ahead / backward / terminal-resurrection edges
        # unless --reopen is given, and `validate` must catch a malformed/unresolvable
        # evidence path (the FID-0046 shape). ROM-free, operates in a throwaway temp
        # ledger dir -- never touches docs/fidelity/ledger/.
        add_test(
            NAME fidelity_ledger_unittest
            COMMAND python3 -m unittest discover -s "${CMAKE_SOURCE_DIR}/tools/fidelity/tests" -p "test_ledger.py"
        )
        set_tests_properties(fidelity_ledger_unittest PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 30)

        # The generated human index docs/fidelity/LEDGER.md must not silently drift
        # from the JSON source of truth (it had fallen 10 findings behind). Guard it.
        add_test(
            NAME fidelity_ledger_index_current
            COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/fidelity/ledger.py" render --check
        )
        set_tests_properties(fidelity_ledger_index_current PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

        # FID-0144: the Release-build guard must compare ge007 only with object
        # files belonging to the ge007 target. Independently rebuilt unit-test
        # objects may be newer than ge007 without making the game binary stale.
        add_test(
            NAME fidelity_release_build_guard
            COMMAND bash "${CMAKE_SOURCE_DIR}/tools/fidelity/tests/test_check_release_build.sh"
        )
        set_tests_properties(fidelity_release_build_guard PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 30)

        # ASM-vs-C audit queue must stay schema-valid with sound verdict-transition
        # integrity (S-tier Task 2.4, FID-0042). ROM-free python guard: validates
        # docs/fidelity/asm_audit_queue.json (verdict grammar, finding-filed FIDs
        # resolve to real ledger entries, ranks are a contiguous 1..N permutation,
        # every referenced source file exists). The queue drives all ~388 GLOBAL_ASM
        # reimplementations to a recorded verdict (S-tier exit criterion 4).
        add_test(
            NAME fidelity_asm_audit_valid
            COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/fidelity/asm_audit.py" validate
        )
        set_tests_properties(fidelity_asm_audit_valid PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

        # Sim-hash coverage audit (S-tier Task 0.4, FID-0030): make "what the
        # sim-state invariance hash covers" a checked property. Every writable
        # (.data/.bss) symbol in the decomp TUs (src/game/* + top-level src/*) must
        # be either hashed by a SimHashRegion or waived in docs/fidelity/
        # hash_waivers.txt with a reason, else this fails. ROM-free: nm over the
        # built ge007 objects + the region table from `ge007 --print-sim-hash-regions`.
        # Needs the built ge007 binary + its objects; SKIPs (not fails) via the
        # prereq wrapper when ge007 has not been built (e.g. a test-only checkout),
        # the same convention as the intro_* smokes. The ge007 target is defined
        # later in this file, so the binary is referenced by path, not $<TARGET_FILE>.
        add_test(
            NAME fidelity_hash_coverage
            COMMAND bash "${CMAKE_SOURCE_DIR}/tools/ctest_require_prereqs.sh"
                --require "${CMAKE_BINARY_DIR}/ge007"
                --
                python3 "${CMAKE_SOURCE_DIR}/tools/fidelity/hash_coverage_audit.py"
                    --binary "${CMAKE_BINARY_DIR}/ge007"
                    --objroot "${CMAKE_BINARY_DIR}/CMakeFiles/ge007.dir"
                    --waivers "${CMAKE_SOURCE_DIR}/docs/fidelity/hash_waivers.txt"
                    --report "${CMAKE_BINARY_DIR}/hash_coverage_report.json"
        )
        set_tests_properties(fidelity_hash_coverage PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            SKIP_RETURN_CODE 125)
    endif()

    # Local MinGW cross-compile lane (backlog MW.2): proves ge007.exe compiles
    # and links for Windows from this host instead of waiting for the release
    # CI. Opt-in (needs `brew install mingw-w64` + network for the vendored
    # SDL2 on first run):
    #   cmake -B build -DPORT_MINGW_CROSS_CHECK=ON && ctest -R mingw_cross_check
    option(PORT_MINGW_CROSS_CHECK
        "Register the MinGW Windows cross-compile check (tools/mingw_cross_check.sh) with CTest" OFF)
    if(PORT_MINGW_CROSS_CHECK)
        add_test(
            NAME mingw_cross_check
            COMMAND bash "${CMAKE_SOURCE_DIR}/tools/mingw_cross_check.sh"
        )
        set_tests_properties(mingw_cross_check PROPERTIES
            LABELS mingw_cross
            TIMEOUT 1800)
    endif()

    # Local PortMaster / GLES3 ARM check (PR #25, repaired c486be2): proves the
    # MGB64_PORTMASTER_GLES target still configures + compiles instead of
    # discovering breaks only on-device. Opt-in, mirroring PORT_MINGW_CROSS_CHECK:
    #   cmake -B build -DPORT_PORTMASTER_GLES_CHECK=ON && ctest -R portmaster_gles_check
    # The script always runs its structural-wiring + host-configure lanes (fail
    # HARD on regression); it attempts a real GLES compile+link when a toolchain
    # is present (Linux GLES dev packages / aarch64 cross / running Docker) and
    # otherwise SKIPs that deep lane (exit 125), deferring compile+link to the CI
    # portmaster job in .github/workflows/release.yml.
    option(PORT_PORTMASTER_GLES_CHECK
        "Register the PortMaster/GLES3 configure+compile check (tools/portmaster_build_check.sh) with CTest" OFF)
    if(PORT_PORTMASTER_GLES_CHECK)
        add_test(
            NAME portmaster_gles_check
            COMMAND bash "${CMAKE_SOURCE_DIR}/tools/portmaster_build_check.sh"
        )
        set_tests_properties(portmaster_gles_check PROPERTIES
            LABELS portmaster
            SKIP_RETURN_CODE 125
            TIMEOUT 1800)
    endif()
endif()

if(BUILD_TESTING AND PORT_VALIDATION_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/baserom.u.z64")
    function(add_port_validation_smoke test_name script_name timeout_seconds)
        set(_port_validation_script "${CMAKE_SOURCE_DIR}/tools/${script_name}")
        if(NOT EXISTS "${_port_validation_script}")
            message(STATUS "Skipping ${test_name}: missing ${_port_validation_script}")
            return()
        endif()
        add_test(
            NAME "${test_name}"
            COMMAND bash "${_port_validation_script}"
                --build-dir "${CMAKE_BINARY_DIR}"
                --rom "${CMAKE_SOURCE_DIR}/baserom.u.z64"
                --no-build
                ${ARGN}
        )
        set_tests_properties("${test_name}" PROPERTIES
            RUN_SERIAL TRUE
            RESOURCE_LOCK ge007_runtime
            PROCESSORS 1
            TIMEOUT "${timeout_seconds}"
        )
    endfunction()

    # Runtime validation smokes. Registered only when PORT_VALIDATION_TESTS is
    # ON and a baserom is present. Every script referenced here must ship in
    # tools/ — enforced by the always-on port_validation_smoke_registry guard
    # above — so this list can never advertise a lane whose script was never
    # committed. (Historically ~38 registrations pointed at scripts that never
    # shipped; those silently skipped, inflating apparent coverage. Removed.)
    add_port_validation_smoke(port_rom_oracle_route_contract route_contract_smoke.sh 900
        --native-smoke
        --all
        --timeout 120
    )
    add_port_validation_smoke(port_campaign_route_smoke campaign_route_smoke.sh 900)
    add_port_validation_smoke(port_dam_progression_smoke dam_progression_smoke.sh 300)
    add_port_validation_smoke(port_surface2_final_flow_smoke surface2_final_flow_smoke.sh 180)
    add_port_validation_smoke(port_hidden_guard_contract_smoke hidden_guard_contract_smoke.sh 240)
    add_port_validation_smoke(port_knife_impact_smoke knife_impact_smoke.sh 180)
    add_port_validation_smoke(port_knife_throw_sfx_smoke knife_throw_sfx_smoke.sh 240)
    # FID-0085: grenade-round (grenade launcher / flare pistol / piton gun) projectile
    # init-matrix hand-field fix. Fires the grenade launcher fix-ON (default) and
    # fix-OFF (GE007_NO_PROJECTILE_INIT_MTX_FIX) and asserts the copied rotation basis
    # differs (the fix is load-bearing -> reddens on a gun.c revert) and that fix-ON is
    # a real orthonormal rotation while the legacy raw+0xAD8 read is not.
    add_port_validation_smoke(port_grenade_round_spawn_mtx_smoke grenade_round_spawn_mtx_smoke.sh 240)
    # FID-0135: hardware-reference screenshots must never silently resample a
    # widescreen drawable into the fixed 640x480 oracle canvas. Proves all three
    # contracts on the default renderer: legacy wide capture remains supported,
    # strict wide capture fails closed with exit 4/no BMP, and strict 4:3 writes.
    add_port_validation_smoke(port_fidelity_capture_aspect_smoke fidelity_capture_aspect_smoke.sh 240)
    # FID-0079: end-to-end guard for the live flag-8 projectile tile-reacquire
    # path (chrobjhandler.c:5328 -> stan.c sub_GAME_7F0AF20C roomset filter).
    # Lobs a proximity mine on Dam so its stan tile is invalidated mid-flight and
    # reacquired; asserts fix-ON reacquires the tile (flag-8 clears) while fix-OFF
    # (GE007_NO_STAN_ROOMSET_BYTE_FIX) stays stuck, and the whole-sim hash differs
    # (fail-on-revert). Complements the pure-TU stan_roomset unit lane.
    add_port_validation_smoke(port_thrown_mine_reacquire_smoke thrown_mine_reacquire_smoke.sh 240)
    # FID-0046: cross-process vanilla determinism. Runs Streets (LEVELID 29 -- the
    # repro level) twice fix-ON and twice fix-OFF and asserts (C) the two fix-ON
    # sim-state hashes are IDENTICAL (the effect-buffer zero-init makes the stage
    # cross-process deterministic) and (D) the two fix-OFF hashes DIFFER (the
    # uninitialized-read defect the fix closes), plus (B) all four screenshots are
    # byte-identical (gameplay unaffected by the flag). Reverting the memset in
    # initexplosioncasing.c reddens assertion (C).
    add_port_validation_smoke(port_streets_determinism_regression streets_determinism_regression.sh 900)
    add_port_validation_smoke(port_minimap_smoke minimap_smoke.sh 900)
    add_port_validation_smoke(port_playability_smoke playability_smoke.sh 180)
    # MC.7 solo-pause: boots a solo level via the app shell, scripts the F1/View
    # settings overlay open for ~11s (> the 5s watchdog threshold) then closed,
    # and asserts (a) the sim clock g_GlobalTimer is FROZEN across the whole pause
    # and resumes after, and (b) the stall watchdog stayed silent (suppression
    # holds). Real-time run — the pause can't engage under --deterministic.
    add_port_validation_smoke(port_overlay_solo_pause_smoke overlay_solo_pause_smoke.sh 180)
    # Post-intro movement gate: boots each level with the intro enabled, lets it
    # play to the FP handoff, then holds forward and asserts the player actually
    # moves -- guards the Silo "look works, WASD dead" regression. Includes a
    # negative control (GE007_NO_POSTINTRO_SPAWN_FIX=1) that must reproduce the
    # frozen state, proving the detector fires.
    add_port_validation_smoke(port_intro_movement_regression intro_movement_regression.sh 600)
    # FID-0006: intro weapon sub-buffer overlap guard. Drives BOTH sides of the R9
    # bounds guard on the shared body/head/weapon load buffer: the positive path
    # (weapon held, 0 RENDER-HEALTH) and the GE007_BOND_WEAPON_BUF_FAIL=1 hatch-forced
    # fail path (exactly 1 [BONDVIEW][RENDER-HEALTH], weapon skipped so
    # right_item_match=0, body intact -- present/rendered equal to the positive path,
    # no crash). Stock Dam leaves ~48KB slack so the fail branch is reachable only via
    # the hatch by design; the prior lane never exercised it (FID-0006 self-admitted
    # gap). Reverting the skip logic makes the hatch a no-op -> this lane reddens.
    add_port_validation_smoke(port_intro_weapon_buf_fail_smoke intro_weapon_buf_fail_smoke.sh 300)
    add_port_validation_smoke(port_renderer_parity_smoke renderer_parity_capture.sh 300)
    # FID-0019 (P1b): Metal sun-shadow path guard. The renderer_parity smoke renders
    # only shadow-FREE scenes, so it never exercised the M3.2 shadow fixes. This lane
    # boots the native Metal backend with sun shadows on Dam and asserts the shadow
    # path is live (shadow map rendered + non-trivial), the 73bbdac nil-fallback stays
    # unreachable in normal play, and both P1b negative-control flags
    # (GE007_NO_METAL_SHADOW_DEPTH_CLAMP / _DUMMY_DEPTH) are wired. Metal-only: SKIPs
    # cleanly on non-Apple hosts and when no Metal device is available. The authored
    # scene also proves the clamp is correctly inert with in-frustum casters; a
    # test-only clip-Z translation then drives those real casters through the near
    # plane and asserts Clamp differs from the legacy Clip negative control.
    add_port_validation_smoke(port_metal_shadow_clamp_regression metal_shadow_clamp_regression.sh 300)
    add_port_validation_smoke(port_surface_xlu_cvg_memory_regression surface_xlu_cvg_memory_regression.sh 360
        --msaa-values 0
    )
    add_port_validation_smoke(port_jungle_xlu_cvg_memory_regression jungle_xlu_cvg_memory_regression.sh 360)
    add_port_validation_smoke(port_spawn_health_smoke spawn_health_check.sh 120)
    # Multi-ammo crate (PROPDEF_AMMO) collect-stride gate: warps Bond onto a
    # Runway crate and asserts the pickup dispenses only the small authored
    # quantities (no 0xFFFF modelnum-lane blowout) with the faithful i+1 ammotype
    # mapping. Guards against the quantities[] overlay regression.
    add_port_validation_smoke(port_ammo_crate_collect_smoke ammo_crate_collect_smoke.sh 180)
    add_port_validation_smoke(port_ammo_hud_smoke ammo_hud_smoke.sh 1800)
    # FID-0064: generate_ammo_total_microcode dropped retail's mpmenuon early-out
    # (gun.c:32354-32356), so an MP pane with the watch/pause menu open still drew
    # its ammo HUD over the darkening overlay. This lane boots a 2p split-screen
    # deathmatch, arms the P2 pane, forces the MP menu open (GE007_AUTO_MPMENU) and
    # asserts the restored early-out hides that pane's ammo HUD -- with a
    # GE007_NO_MP_AMMO_HUD_MENU_FIX=1 negative control that reproduces the legacy
    # draw. Reverting the fix makes open_on draw the HUD too (delta -> 0) -> red.
    add_port_validation_smoke(port_ammo_hud_mpmenu_smoke ammo_hud_mpmenu_smoke.sh 300)
    # Per-level frame-time budget gate (docs/design/PERFORMANCE_PLAN.md M0). Reference-
    # hardware sensitive; opt-in and serialized like the other runtime lanes.
    add_port_validation_smoke(port_perf_budget_smoke perf_budget_smoke.sh 900)
    # FID-0007 (b): dyn-allocator fail-closed contract on the glass-shard float-
    # matrix render path. Forces VTX-arena overflow (GE007_DYN_STRESS_LIMIT) on the
    # Dam regular-glass shatter route and asserts the shard path skips overflowed
    # pieces (zero screen-spanning triangles, only dyn_overflow moves) with a
    # GE007_DYN_LEGACY_ALIAS=1 A/B negative control that restores the old aliasing.
    add_port_validation_smoke(port_dyn_glass_stress_smoke dyn_glass_stress_smoke.sh 400)
    # FID-0054: combat-route determinism guard. Replays the committed Dam combat
    # input tape (baselines/tapes/dam_combat_guard6.ge7tape) — which FIRES the PP7,
    # spawns projectiles and provokes guard-AI reactions — and asserts the whole-sim
    # state hash matches the recorded baseline. Fail-on-divergence proves combat
    # stays deterministic under the seed envelope (the precondition for frame-locking
    # the guard-AI parity divergence against the ares oracle). ares-free / ROM-gated.
    add_port_validation_smoke(port_combat_route_capture_smoke fidelity/combat_route_capture_smoke.sh 180)
    # FID-0009: Silo intro-swirl aperture leak. During the SWIRL camera the detached
    # camera orbits inside Bond's own room, so the T13b draw-only camera-seed walk was
    # dormant and the grazing far-wall room dropped to the sky-blue clear color. Boots
    # the Silo intro to the offending swirl pose and asserts (a) the fix admits the
    # grazing room DRAW-ONLY so the aperture renders geometry (blue-sky px collapse),
    # (b) every sim-consumed field is byte-identical fix vs the GE007_NO_CAMERA_SEED_MULTIHOP
    # opt-out (draw-only never enters the sim), and (c) the opt-out reproduces the leak
    # (fail-on-revert control).
    add_port_validation_smoke(port_silo_intro_aperture_regression silo_intro_aperture_regression.sh 300)
    # FID-0008: Train room-51 window grazing-aperture fill under --faithful. The
    # wideners are OFF under --faithful (they widen room_rendered and perturb the sim
    # RNG), so the pure BFS drops room 53 behind the venetian-slat window and the fill
    # is wrong. Boots Train to pad 74 with --faithful and asserts (a) the fix admits
    # room 53 DRAW-ONLY (window covered, never sim-rendered), (b) every sim-consumed
    # field is byte-identical fix vs the GE007_NO_FAITHFUL_DRAW_ONLY_WIDENERS opt-out,
    # and (c) the opt-out drops room 53 and reverts the fill (fail-on-revert control).
    add_port_validation_smoke(port_train_faithful_aperture_regression train_faithful_aperture_regression.sh 300)
    # FID-0066: NPC full-auto cadence symmetry (the port-half of the FID-0056
    # parity skew). Replays dam_forward_30s — on which guard chr=6 (KF7, rate=3)
    # sustains full-auto — and, from the GE007_TRACE_GUARD_AUTOFIRE gate-fire
    # stream, asserts the DEFAULT (fix-ON) within-burst inter-shot interval is
    # exactly FrameCost(=3)x the GE007_FIRE_RATE_AUTHENTIC=0 (opt-out) interval,
    # AND the opt-out sim-hash is byte-identical to the recorded pre-fix baseline.
    # Exercises the real chrlvFireWeaponRelated call site end-to-end: a divisor-
    # scaling revert collapses the ON interval to the OFF interval (ratio 1x) and
    # reddens this lane. Two full 1802-tick replays -> 300s. ROM-gated / ares-free.
    add_port_validation_smoke(port_guard_fire_rate_symmetry_smoke fidelity/guard_fire_rate_symmetry_smoke.sh 300)
    # FID-0056 / P1d: end-to-end gate for the full-auto fire-cadence fix's gun.c
    # CALL-SITE. Replays a committed AK47 sustained-fire tape (baselines/tapes/
    # dam_ak47_sustained.ge7tape) that holds the full-auto machinegun case for 400
    # ticks — the case the PP7 combat tape never reaches and the ROM-free unit test
    # only re-implements. Input.FireRateAuthentic ships ON (faithful N64 cadence),
    # so the gate asserts the authentic hash; a call-site revert makes the default
    # behave like the legacy locked-60Hz cadence, diverging the hash and failing
    # this lane. ares-free / ROM-gated. (--prove self-tests authentic != legacy.)
    add_port_validation_smoke(port_ak47_fire_rate_capture_smoke fidelity/ak47_fire_rate_capture_smoke.sh 180)
    # FID-0014: faithful WAYMODE_MAGIC patrol semantics. Native-only capture of
    # dam_combat_guard6_agealign twice (fix ON default / GE007_NO_PATROL_MAGIC_FIX=1),
    # profiled with tools/fidelity/patrol_profile.py over the pre-onset window.
    # Asserts the retail pause/warp profile natively (chr45 >=90% paused with a
    # >=1000u pad warp; chr41/42/43 >=50% paused) AND the negative control
    # (opt-out -> legacy creep-walk: chr45 <=5% paused, no >=1000u warp).
    # Fail-on-revert: removing the chr.c magic dispatch (or a flag-polarity bug)
    # reddens the ON side; the OFF side pins the byte-identical legacy path.
    # Two ~682-frame native captures -> 900s. ares-free / ROM-gated.
    add_port_validation_smoke(port_patrol_magic_profile_smoke fidelity/patrol_magic_profile_smoke.sh 900)
    # Phase B (FID-0031 / D4): ares-free hard gate for gate:true ROM-oracle routes.
    # Enumerates every route marked "gate": true and replays its paired determinism
    # tape (baselines/tapes/<tape>.ge7tape), asserting the sim-state hash. Enforces
    # the route<->tape linkage: a route cannot claim gate:true without a passing
    # tape. SKIPs cleanly (and passes on an empty gate set). ares-free / ROM-gated.
    add_port_validation_smoke(port_gate_routes_smoke fidelity/gate_routes_smoke.sh 600)
    # FID-0044 (Task 2.2, criterion 2): S2 pixel-oracle ratchet at the ctest layer
    # (previously only in docs/fidelity/verify_manifest.txt). Runs the per-stage
    # native-vs-ares screenshot sweep in --gate mode: for every "gate": true pixel
    # checkpoint it captures the native BMP at the checkpoint game-timer, pairs it
    # with the cached ares stock PPM, normalizes, diffs, clusters, and classifies
    # against docs/fidelity/APPROXIMATIONS.md — failing iff any cluster is NOT
    # attributable to a documented approximation class. SKIPs cleanly (exit 0) when
    # the ROM, native binary, ares binary, or a checkpoint's cached stock PPM is
    # absent, and passes on an empty gate-checkpoint set. ares+ROM-gated. The
    # ROM-free injected-regression proof (broken pair -> unexplained cluster) is the
    # sibling sense_pixel_sweep_selftest ctest.
    add_port_validation_smoke(port_pixel_oracle_sweep_gate fidelity/sense_pixel_sweep.sh 600 --gate)
endif()

# T5: cheap intro-path regression
# gates. Unlike add_port_validation_smoke above (opt-in via
# PORT_VALIDATION_TESTS, and invisible entirely without baserom.u.z64 at
# CMake-configure time), these three are ALWAYS registered so a ROM-less
# checkout (CI) still enumerates them and reports a genuine SKIPPED result
# instead of the test simply not existing. Each entry runs through
# tools/ctest_require_prereqs.sh, which checks the ROM/binary/ares
# prerequisite itself and exits SKIP_RETURN_CODE (125) before invoking the
# real script — the underlying scripts' own missing-prereq paths already
# exit 2 (see e.g. intro_parse_digest_gate.sh's header comment), but that
# exit 2 is shared with unrelated argument/validation failures in the same
# scripts, so it cannot be claimed directly as SKIP_RETURN_CODE without
# turning real failures into silent skips.
if(BUILD_TESTING)
    function(add_intro_gate_smoke)
        set(oneValueArgs NAME TIMEOUT)
        set(multiValueArgs REQUIRES COMMAND)
        cmake_parse_arguments(AIG "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

        set(_require_args "")
        foreach(_req IN LISTS AIG_REQUIRES)
            list(APPEND _require_args --require "${_req}")
        endforeach()

        add_test(
            NAME "${AIG_NAME}"
            COMMAND bash "${CMAKE_SOURCE_DIR}/tools/ctest_require_prereqs.sh"
                ${_require_args}
                --
                ${AIG_COMMAND}
        )
        set_tests_properties("${AIG_NAME}" PROPERTIES
            SKIP_RETURN_CODE 125
            TIMEOUT "${AIG_TIMEOUT}"
            RUN_SERIAL TRUE
            RESOURCE_LOCK ge007_runtime
            PROCESSORS 1
        )
    endfunction()

    # T3 parse-digest gate — 3-stage subset for a sub-90s ctest lane. The
    # full 20-stage sweep stays a manual/release-lane invocation
    # (tools/intro_parse_digest_gate.sh with no --stages override).
    add_intro_gate_smoke(
        NAME intro_parse_digest_smoke
        TIMEOUT 90
        REQUIRES
            "${CMAKE_SOURCE_DIR}/baserom.u.z64"
            "${CMAKE_BINARY_DIR}/ge007"
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/intro_parse_digest_gate.sh"
            --rom "${CMAKE_SOURCE_DIR}/baserom.u.z64"
            --binary "${CMAKE_BINARY_DIR}/ge007"
            --stages "33 34 22"
    )

    # T2/T4-hardened Dam intro camera-path oracle route: full native capture
    # + ares stock capture + comparator run. HEAVYWEIGHT (~4-6 min) — LABELed
    # "oracle" so `ctest -L oracle` selects it independent of the `intro_`
    # name filter used for the other two. SKIPs (not fails) when the
    # instrumented ares binary is absent, via the same prerequisite-wrapper
    # mechanism as the other two entries.
    add_intro_gate_smoke(
        NAME intro_oracle_dam_route
        TIMEOUT 900
        REQUIRES
            "${CMAKE_SOURCE_DIR}/baserom.u.z64"
            "${CMAKE_BINARY_DIR}/ge007"
            "${CMAKE_SOURCE_DIR}/build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares"
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/movement_oracle_capture.sh"
            --route dam_intro_camera_path
            --build-dir "${CMAKE_BINARY_DIR}"
            --rom "${CMAKE_SOURCE_DIR}/baserom.u.z64"
            --no-build
            --ares-bin "${CMAKE_SOURCE_DIR}/build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares"
            --timeout 600
    )
    set_tests_properties(intro_oracle_dam_route PROPERTIES LABELS oracle)

    # FID-0029 / D36/D41: the camera-path route does not compare Bond's animation.
    # Keep the post-D43 three-phase verdict as a separate ares-backed oracle.
    # It compares camera mode/setup/Bond only (the route above owns randomized
    # camera selection and D31/D35), event-aligns the retail three-tick idle
    # boundary, and compares phase 3 at equal animation age.
    add_intro_gate_smoke(
        NAME intro_oracle_dam_bond_anim
        TIMEOUT 900
        REQUIRES
            "${CMAKE_SOURCE_DIR}/baserom.u.z64"
            "${CMAKE_BINARY_DIR}/ge007"
            "${CMAKE_SOURCE_DIR}/build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares"
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/movement_oracle_capture.sh"
            --route dam_intro_swirl_bond_anim
            --build-dir "${CMAKE_BINARY_DIR}"
            --rom "${CMAKE_SOURCE_DIR}/baserom.u.z64"
            --no-build
            --ares-bin "${CMAKE_SOURCE_DIR}/build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares"
            --timeout 600
    )
    set_tests_properties(intro_oracle_dam_bond_anim PROPERTIES LABELS oracle)

    # Single-stage (Dam) census smoke, reduced frame count for a sub-60s
    # ctest lane; tools/intro_census_capture.sh --all (20 stages) is the
    # manual/release-lane invocation.
    add_intro_gate_smoke(
        NAME intro_census_dam_smoke
        TIMEOUT 60
        REQUIRES
            "${CMAKE_SOURCE_DIR}/baserom.u.z64"
            "${CMAKE_BINARY_DIR}/ge007"
        COMMAND bash "${CMAKE_SOURCE_DIR}/tools/intro_census_capture.sh"
            --level 33
            --frames 400
            --build-dir "${CMAKE_BINARY_DIR}"
            --rom "${CMAKE_SOURCE_DIR}/baserom.u.z64"
            --no-build
    )
endif()

# --- Compile definitions ---
add_definitions(
    -DNONMATCHING
    -DNATIVE_PORT
    -DPORT_FIXME_STUBS
    -D_LANGUAGE_C
    -DVERSION_US
    -DLANG_US
    -DREFRESH_NTSC
    -DLEFTOVERDEBUG
    -DLEFTOVERSPECTRUM
    -DBUGFIX_R0
    -DBYTEMATCH
    # We provide our own main() (main_pc.c) and call SDL_SetMainReady() first
    # thing in it, rather than letting SDL_main.h's macro rename it to
    # SDL_main and require linking SDL2main. This already worked "by
    # accident" because main_pc.c never included SDL.h; this makes it an
    # officially supported configuration instead.
    -DSDL_MAIN_HANDLED
)
# Note: TARGET_N64 is NOT defined — activates PC code paths

# --- Collect sources ---
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS src/game/*.c)
file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS src/platform/*.c)

# Exclude an intentionally empty legacy audio-ring placeholder. The active
# native audio backend is audio_pc.c.
list(FILTER PLATFORM_SOURCES EXCLUDE REGEX "audio_ring\\.c$")
# platform_titan.c owns the console-only SDL replacement and must not collide
# with platform_sdl.c in normal desktop builds.
list(FILTER PLATFORM_SOURCES EXCLUDE REGEX "platform_titan\\.c$")

# Single-sourced CLI stage/difficulty/MP tables (AUDIT-0060): moved verbatim out
# of main_pc.c so the launcher's launch_intent parser and ui_launch.cpp share
# them. Lives in src/app/ but compiles with the engine (it needs bondconstants.h
# for the enum values, and main_pc.c/decor_native.c need its symbols even with
# MGB64_APP=OFF). mgb64_app's references resolve at the final ge007 link against
# this single copy — the .cpp-only APP_SOURCES glob never picks it up.
list(APPEND PLATFORM_SOURCES "${CMAKE_SOURCE_DIR}/src/app/cli_stage_tables.c")

# Fast3D display list interpreter (replaces old gfx_pc.c)
set(FAST3D_SOURCES
    src/platform/fast3d/gfx_pc.c
    src/platform/fast3d/screenshot_series.c
    src/platform/fast3d/gfx_opengl.c
    src/platform/fast3d/gfx_cc.c
    src/platform/fast3d/gfx_backend.c
    src/platform/fast3d/gfx_room_normals.c
    src/platform/fast3d/gfx_msaa_util.c
)
# Native Metal backend (macOS-only, opt-in at runtime). GL TU stays on all platforms.
if(APPLE)
    list(APPEND FAST3D_SOURCES src/platform/fast3d/gfx_metal.mm)
    # ARC for the Metal TU only (manages id<MTL...>/CAMetalLayer/drawable lifetimes).
    set_source_files_properties(src/platform/fast3d/gfx_metal.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

# Cross-platform WebGPU (wgpu-native) backend — opt-in at build time
# (MGB64_WEBGPU_BACKEND) AND at runtime (GE007_RENDERER=webgpu). Compiled into
# the game only when the option is on, keeping the default build byte-identical
# (no wgpu symbols) until the deliberate default flip. The `webgpu` INTERFACE
# target (include dir + pinned prebuilt lib + platform frameworks) is defined by
# cmake/webgpu.cmake; it is linked into the engine targets below.
if(MGB64_WEBGPU_BACKEND)
    list(APPEND FAST3D_SOURCES
        src/platform/fast3d/gfx_webgpu.c
        src/platform/fast3d/gfx_webgpu_shader.c)
endif()

# glad OpenGL loader — not needed on macOS (uses framework) or GLES (SDL handles it)
if(NOT APPLE AND NOT MGB64_PORTMASTER_GLES)
    set(GLAD_SOURCES lib/glad/src/glad.c)
else()
    set(GLAD_SOURCES "")
endif()

# System sources - select only what we need
set(SYSTEM_SOURCES
    src/boss.c
    src/init.c
    src/memp.c
    src/mema.c
    src/sched.c
    src/fr.c
    src/vi.c
    src/joy.c
    src/music.c
    src/snd.c
    src/deb.c
    src/debugmenu.c
    src/speed_graph.c
    src/random.c
    src/token.c
    src/ramrom.c
    src/crash.c
    src/cfb.c
    src/stacks.c
    src/rmon.c
    src/pi.c
    src/tlb_manage.c
    src/tlb_random.c
    src/c_data_filler.c
)

# Native builds must not compile SDK-derived libultra audio source. Keep this
# explicit so SDK audio files cannot silently enter the target through a glob.
set(LIBULTRA_AUDIO_SOURCES
)
# These compile with NATIVE_PORT; libaudio.h is now ungated so types are available
set(LIBULTRARE_AUDIO_SOURCES
)
# libultrare also compiles with NATIVE_PORT

# Exclude files that are N64-only or need special handling
# (boot.c, motor.c, str.c, sprintf.c, usb.c)

# Asset sources.
#
# These are first-party / code-coupled source (display lists, descriptor tables,
# offset metadata) that contain NO ROM media payload, so they always ship:
#   font_dl.c          - basic-geometry display lists
#   GlobalImageTable.c - image display lists + symbolic descriptor tables
set(ASSET_SOURCES
    assets/font_dl.c
    assets/GlobalImageTable.c)
#
# These are ROM-DERIVED BULK DATA (animation frames, the Rareware logo image) and
# are intentionally NOT distributed (see DISCLAIMER.md / docs/STATUS.md). The port
# loads this data from YOUR ROM at runtime, so it builds and links without them.
# They are compiled in automatically only if you have extracted them into assets/.
foreach(_romdata
        assets/animationtable_data.c
        assets/animationtable_entries.c
        assets/rarewarelogo.c)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${_romdata}")
        list(APPEND ASSET_SOURCES ${_romdata})
    else()
        message(STATUS "MGB64: ROM-derived asset data not present (not distributed): ${_romdata} -- see docs/STATUS.md")
    endif()
endforeach()

# --- Build executable ---
option(PORT_SOUNDPLAYER_REAL "Compile snd.c's real ALSndPlayer path while keeping global port stubs enabled" ON)

add_executable(ge007
    ${GAME_SOURCES}
    ${SYSTEM_SOURCES}
    ${ASSET_SOURCES}
    ${PLATFORM_SOURCES}
    ${FAST3D_SOURCES}
    ${GLAD_SOURCES}
    ${LIBULTRA_AUDIO_SOURCES}
    ${LIBULTRARE_AUDIO_SOURCES}
)

if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/branding/appicon-windows.ico")
    target_sources(ge007 PRIVATE ${CMAKE_SOURCE_DIR}/windows/mgb64.rc)
    # Feed the derived version into windres (no embedded quotes: mgb64.rc
    # stringizes MGB64_VER_STR itself). Scoped to the .rc so the version defines
    # never reach the C/C++ compile of the engine.
    set_source_files_properties(${CMAKE_SOURCE_DIR}/windows/mgb64.rc PROPERTIES
        COMPILE_DEFINITIONS "MGB64_VER_CSV=${MGB64_VER_CSV};MGB64_VER_STR=${MGB64_VER_STR}")
endif()

if(PORT_SOUNDPLAYER_REAL)
    target_compile_definitions(ge007 PRIVATE PORT_SOUNDPLAYER_REAL)
endif()

# WebGPU backend: gate the code (MGB64_WEBGPU_BACKEND) and link the pinned
# prebuilt (the `webgpu` INTERFACE target from cmake/webgpu.cmake). Only when the
# option is on, so the default build links no wgpu symbols (byte-identical).
if(MGB64_WEBGPU_BACKEND)
    target_compile_definitions(ge007 PRIVATE MGB64_WEBGPU_BACKEND)
    # Plain signature to match the other target_link_libraries(ge007 ...) calls
    # (CMake forbids mixing plain and keyword forms on one target). The `webgpu`
    # INTERFACE target's include dirs + frameworks still propagate transitively.
    target_link_libraries(ge007 webgpu)
endif()

target_include_directories(ge007 PRIVATE
    include
    include/PR
    src
    src/game
    src/platform
    src/platform/fast3d
    src/libultra/audio
    assets
    lib/glad/include
    lib/stb
    lib/cgltf
    .
)

option(PORT_STRICT "Enable strict pointer safety warnings as errors (audit tool)" OFF)

# --- Diagnostic configuration ---
# The default build suppresses 12+ diagnostic categories to allow compilation
# of NONMATCHING decompiled functions that use N64-era pointer/integer casts.
# This is intentional for development velocity — the suppressed warnings are
# real type issues but only matter on 64-bit (arm64/x86_64) targets.
#
# PORT_STRICT re-enables a subset of these as -Werror to audit pointer safety:
#   cmake -B build-strict -DPORT_STRICT=ON .
# Expect errors in files with active NONMATCHING code that hasn't been
# ported yet (bg.c, bondhead.c, bondinv.c, blood_decrypt.c, etc.).
# Errors inside PORT_FIXME_STUBS blocks are expected and harmless.
target_compile_options(ge007 PRIVATE
    -Wall
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-missing-braces
    -Wno-parentheses
    -Wno-unknown-pragmas
    -Wno-implicit-function-declaration
    -Wno-missing-declarations
    -Wno-int-conversion
    -Wno-incompatible-pointer-types
    -Wno-empty-body
    -Wno-switch
    # -Wno-return-type  — REMOVED: all return paths verified
    # -Wno-return-mismatch  — REMOVED: all return types verified
    -fms-extensions
    # Force the SysV/Itanium bit-field layout on EVERY platform. This decomp
    # port relies on the original N64 struct layouts (see -fno-strict-aliasing
    # below), but MinGW/GCC on Windows DEFAULTS to -mms-bitfields (MSVC-compatible
    # bit-field packing), which changes the size and field offsets of the game's
    # mixed bit-field structs — e.g. `struct {u8 a; unsigned x:8; u16 y:12; u32 addr;}`
    # goes from 8 bytes / addr@4 to 16 bytes / addr@12. That desyncs every struct
    # read over ROM/model/setup data from the bytes actually there, which is the
    # Windows-only immediate-crash-on-load. macOS/Linux already default to this
    # layout, so the flag is a no-op there and keeps all three targets in sync.
    # (x86-only: ARM toolchains reject the flag, and the MS layout it disables
    # never existed off x86 -- guarded so PortMaster/ARM builds configure.)
    ${MGB64_MS_BITFIELD_FLAG}
    # This is a decomp port: pointer punning between the original N64 struct
    # layouts and PC-native types (ROM_OFFSET in platform_os.h, bg.c/gun.c/
    # chrobjhandler.c/bondview.c reinterpret casts) is pervasive and relied
    # upon for byte-faithful behavior. gcc -O3 acts on strict-aliasing UB
    # (92 warnings on gcc-13); clang/macOS is unaffected either way.
    -fno-strict-aliasing
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(ge007 PRIVATE
        -Wno-macro-redefined
        -Wno-initializer-overrides
        -Wno-flexible-array-extensions
        -Wno-gnu-flexible-array-initializer
        -Wno-invalid-token-paste
        -Wno-microsoft-anon-tag
        -ferror-limit=0
    )
endif()

option(SANITIZE "Enable UBSan + ASan for crash diagnosis" OFF)
if(SANITIZE)
    target_compile_options(ge007 PRIVATE -fsanitize=undefined,address -fno-omit-frame-pointer -O1 -g)
    target_link_options(ge007 PRIVATE -fsanitize=undefined,address)
endif()

if(PORT_STRICT)
    target_compile_options(ge007 PRIVATE
        -Werror=int-conversion
        -Werror=incompatible-pointer-types
        -Werror=pointer-to-int-cast
        -Werror=int-to-pointer-cast
        -Werror=implicit-function-declaration
        -Werror=return-type
        -Werror=pointer-integer-compare
        -Werror=conditional-type-mismatch
    )
endif()

if(APPLE AND (PORT_STRICT OR SANITIZE))
    target_compile_options(ge007 PRIVATE -Werror=deprecated-declarations)
endif()

if(APPLE)
    target_link_libraries(ge007
        ${OPENGL_LIB}
        m
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Foundation"
    )
    if(SDL2_FOUND)
        target_link_libraries(ge007 SDL2::SDL2)
    elseif(SDL2_LIBRARIES)
        target_include_directories(ge007 PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(ge007 ${SDL2_LIBRARIES})
    endif()
elseif(WIN32)
    target_link_libraries(ge007
        OpenGL::GL
        SDL2::SDL2
        # clock_gettime(CLOCK_MONOTONIC) (stubs.c, gfx_room_normals.c) is a
        # winpthread symbol on mingw-w64. It's already pulled in by the
        # posix-threads variant's default libs, but the (also common)
        # win32-threads variant needs it explicit on the link line or it's
        # an undefined reference (probe-verified both ways under Docker +
        # x86_64-w64-mingw32-gcc-win32/-posix). Harmless on posix-threads.
        # Bracketed -Bstatic/-Bdynamic so ld takes the STATIC winpthread archive
        # here (the first mention wins symbol resolution) — otherwise the exe
        # imports libwinpthread-1.dll, absent on stock Windows.
        "-Wl,-Bstatic" winpthread "-Wl,-Bdynamic"
    )
    # GUI subsystem by default (clean launch, no console window). MinGW keeps
    # mainCRTStartup -> our int main() under -mwindows (SDL_MAIN_HANDLED, no
    # WinMain needed), so this only flips the PE subsystem + GUI import libs.
    if(NOT MGB64_WIN_CONSOLE)
        target_link_options(ge007 PRIVATE -mwindows)
    endif()
    # Static-link the GCC runtimes: a MinGW-built exe otherwise imports
    # libgcc_s_seh-1.dll / libstdc++-6.dll / libwinpthread-1.dll, which exist on
    # the BUILD machine's toolchain but not on stock Windows ("The code
    # execution cannot proceed because libgcc_s_seh-1.dll was not found").
    # SDL2.dll stays dynamic (shipped beside the exe); winpthread must be
    # bracketed -Bstatic/-Bdynamic at the END of the link line so the import
    # lib above is overridden by the static archive.
    target_link_options(ge007 PRIVATE -static-libgcc -static-libstdc++)
elseif(MGB64_PORTMASTER_GLES)
    target_link_libraries(ge007
        GLESv2
        EGL
        SDL2::SDL2
        m
        ${CMAKE_DL_LIBS}
    )
    target_compile_definitions(ge007 PRIVATE MGB64_PORTMASTER_GLES MGB64_PORTMASTER)
elseif(EMSCRIPTEN)
    # Web build links the wasm engine as `ge007_web` (below); the native `ge007`
    # target is never built under wasm, so it gets no platform libraries here
    # (OpenGL::GL / SDL2::SDL2 don't exist under the Emscripten toolchain).
else()
    target_link_libraries(ge007
        OpenGL::GL
        SDL2::SDL2
        m
        # dladdr() in src/random.c; folded into libc on glibc >= 2.34 but a
        # separate libdl on older distros.
        ${CMAKE_DL_LIBS}
    )
endif()

if(EMSCRIPTEN)
    # Web build: engine only (the HTML page is the launcher). Single-threaded,
    # Asyncify-driven frame loop, MEMFS+IDBFS filesystem. Everything here is
    # additive — native configs never enter this branch. The source variables
    # (${GAME_SOURCES} etc.) are the exact list the native `ge007` target is
    # built from above (:1715); the app shell (mgb64_app) is a separate library
    # and is never in this list, so no app source enters the wasm binary.
    set(MGB64_APP OFF CACHE BOOL "" FORCE)          # no ImGui launcher in v1 web
    set(MGB64_WEBGPU_BACKEND ON CACHE BOOL "" FORCE)
    add_executable(ge007_web
        ${GAME_SOURCES}
        ${SYSTEM_SOURCES}
        ${ASSET_SOURCES}
        ${PLATFORM_SOURCES}
        ${FAST3D_SOURCES}
        ${GLAD_SOURCES}
        ${LIBULTRA_AUDIO_SOURCES}
        ${LIBULTRARE_AUDIO_SOURCES}
    )
    target_link_libraries(ge007_web PRIVATE webgpu)
    target_compile_definitions(ge007_web PRIVATE NATIVE_PORT MGB64_WEBGPU_BACKEND)
    # WEB-006: compile snd.c's real ALSndPlayer path (matching ge007/:1767 and
    # ge007_lib/:2307). Without it snd.h selects PORT_SND_STUBS (the directory-wide
    # PORT_FIXME_STUBS applies), so the web build silently shipped the legacy stub
    # sound player — an unlabeled native/web engine divergence in a "same engine"
    # product. Gated so PORT_SOUNDPLAYER_REAL=OFF still turns it off everywhere.
    if(PORT_SOUNDPLAYER_REAL)
        target_compile_definitions(ge007_web PRIVATE PORT_SOUNDPLAYER_REAL)
    endif()
    # Mirror ge007's include search path (:1755) so the shared engine sources
    # resolve identically (notably the source-root "." for assets/images.def).
    target_include_directories(ge007_web PRIVATE
        include
        include/PR
        src
        src/game
        src/platform
        src/platform/fast3d
        src/libultra/audio
        assets
        lib/glad/include
        lib/stb
        lib/cgltf
        .
    )
    target_link_options(ge007_web PRIVATE
        "-sUSE_SDL=2"
        # PERF-031: narrow the Asyncify instrumentation set. Blanket -sASYNCIFY
        # instruments EVERY call path that could reach a suspend (the whole
        # ~3.4 MB code section) with spill/restore scaffolding — 1.5-3x on
        # instrumented paths — for a handful of real suspend sites. Measured
        # effect of the narrowing below: wasm 4,020,482 -> 3,118,775 B (-22.4%),
        # a cheap proxy for the per-frame CPU win. IGNORE_INDIRECT stops
        # reachability inference at indirect (function-pointer) call edges, so
        # only functions reaching a suspend via DIRECT calls auto-instrument; ADD
        # force-instruments the functions that OWN an indirect suspend-reaching
        # call (an auto-instrumented function does NOT scaffold its indirect call
        # sites — only an ADD-listed one does; that scaffolding is what lets the
        # stack unwind/rewind through the function pointer).
        #
        # Suspend imports (confirmed via -sASYNCIFY_ADVISE): emscripten_sleep,
        # platformWaitAnimationFrame (the EM_ASYNC_JS rAF wait), and __wasi_fd_sync
        # (fflush/close/EEPROM-save file sync — an emscripten default suspend import).
        #
        # Suspend sites and how each is covered:
        #   1. rAF wait + hidden-tab emscripten_sleep — both DIRECT in
        #      platformFrameSync, which -O2 inlines into osRecvMesg (ADDed). The
        #      hidden-tab sleep shares that function, so it is covered by the same
        #      entry (can't be exercised headless; covered by construction).
        #   2. portLoadYield emscripten_sleep(0) (PERF-035 level-load yields) —
        #      lvlStageLoad / load_bg_file inline into bossMainloop -> bossEntry;
        #      portLoadYield is ADDed as the leaf.
        #   3. WGPU_COMPAT_WAIT bring-up pumps (gfx_webgpu_bringup, emscripten_sleep)
        #      — reached from gfx_init through the gfx_rapi->init FUNCTION POINTER.
        #      gfx_init inlines into MAIN, so that indirect edge now lives in main;
        #      `main` MUST be ADDed or its rewind mis-dispatches (RuntimeError:
        #      unreachable at first game-loop suspend — the crash this iteration hit
        #      and fixed). `bossEntry` is likewise ADDed for the game loop's own
        #      indirect dispatch edges.
        #   4. WGPU_COMPAT_WAIT readback pumps (wgpu_write_ppm buffer-map,
        #      wgpu_read_framebuffer_rgb) — DELIBERATELY EXCLUDED. Armed only by
        #      GE007_WEBGPU_DUMP_FRAME/_SURFACE getenv() or the native screenshot/
        #      parity capture path; the browser build exposes no way to set those
        #      env vars (no ENV export, no preRun setenv, the shell passes only
        #      ?arg= CLI flags) and the frame-probe harness reads the canvas over
        #      CDP, never the in-wasm readback — so this chain never fires in the
        #      deployed/tested web build. The two leaf functions still auto-
        #      instrument (they call the sleep directly) — harmless — but we do NOT
        #      bridge their indirect (gfx_rapi->read_framebuffer_rgb / getenv-gated
        #      end_frame) callers into the set.
        #
        # The ADD list names the whole logical suspend spine. LOAD-BEARING today:
        # main + bossEntry (own the indirect edges above), osRecvMesg + wgpu_init +
        # portLoadYield (real leaf/near-leaf owners). The rest (gfx_init,
        # gfx_webgpu_bringup, platformFrameSync, lvlStageLoad, load_bg_file,
        # bossMainloop) are inlined away by -O2 and emit a benign "Asyncify addlist
        # contained a non-existing function name" warning at link — kept
        # DELIBERATELY: they document the chain AND stay correct if a future
        # refactor (or the LTO pass below) de-inlines one back into a standalone
        # function that owns an indirect edge. None of these pull in a callee
        # subtree (ASYNCIFY_PROPAGATE_ADD walks callers, not callees), so the hot
        # per-triangle emit / audio DSP / sim loops stay OUT of the set (the win).
        # A too-narrow list traps LOUDLY at the missed suspend at runtime — the
        # iteration signal from the web_boot_smoke / web_frame_probe gates.
        "-sASYNCIFY"
        "-sASYNCIFY_STACK_SIZE=65536"
        "-sASYNCIFY_IGNORE_INDIRECT"
        "-sASYNCIFY_ADD=main,gfx_init,wgpu_init,gfx_webgpu_bringup,platformFrameSync,osRecvMesg,portLoadYield,lvlStageLoad,load_bg_file,bossMainloop,bossEntry"
        "-sALLOW_MEMORY_GROWTH=1"
        # PERF-033: real high-water is ~30 MB (12 MB ROM + 8 MB sim pool + GPU
        # staging), so 256 MiB was a large upfront commit that raised boot-failure
        # and tab-eviction odds on the marginal devices (iOS Safari, low-RAM
        # Android/Chromebooks) broad playability targets. Drop the initial commit
        # to 128 MiB (still ~4x real usage) and let growth absorb any overflow;
        # MAXIMUM_MEMORY documents the 512 MiB ceiling.
        "-sINITIAL_MEMORY=134217728"      # 128 MiB
        "-sMAXIMUM_MEMORY=536870912"      # 512 MiB ceiling for ALLOW_MEMORY_GROWTH
        "-sSTACK_SIZE=2097152"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=createMGB64"
        "-sINVOKE_RUN=0"                  # shell seeds FS first, then callMain
        "-sEXIT_RUNTIME=0"
        "-sEXPORTED_RUNTIME_METHODS=callMain,FS,IDBFS"
        "-lidbfs.js"
        "-sENVIRONMENT=web"
        # WEB-057: Debug-config-only web sanitizers, wired via generator
        # expression so Release carries neither (both add real runtime cost).
        # SAFE_HEAP is precisely the ILP32 out-of-bounds/misaligned-access tool
        # this wasm32 decomp wants; ASSERTIONS=2 turns on emscripten's most
        # verbose runtime checks. build_web.sh --debug drives CMAKE_BUILD_TYPE=Debug;
        # keep docs/WEB.md's --debug line in sync with these flags.
        "$<$<CONFIG:Debug>:-sSAFE_HEAP=1>"
        "$<$<CONFIG:Debug>:-sASSERTIONS=2>"
        # PERF-034 (LTO) — RE-ATTEMPTED on top of the PERF-031 narrowing above and
        # REJECTED AGAIN (measured 2026-07-18). Adding "$<$<CONFIG:Release>:-flto>"
        # to the web compile + link inflated the wasm 3,118,775 -> 3,995,430 B
        # (+28%), NOT the hoped-for shrink. The prior theory — that whole-program
        # -sASYNCIFY was the amplifier and a narrow ADD list would flip LTO to a
        # win — did NOT hold: LTO's own cross-module inlining duplicates code (and
        # re-instruments each inlined copy that reaches a suspend), so the net is
        # still a large inflation even with the narrow Asyncify set. Removed; the
        # narrowing alone is the win. (--closure=1 stays OFF too — still fails the
        # build on SDL2's legacy allocate()/ALLOC_NORMAL message-box ASM_CONST.)
    )
    # Layout-critical + warning flags: the wasm engine compiles the SAME decomp
    # game TUs as native ge007 (:1783), so it needs the identical parse-affecting
    # flags. Chief among them: -fms-extensions, which enables the anonymous
    # nested-struct member the `inherits struct` idiom (bondtypes.h:42) relies on
    # for field inheritance (ObjectRecord.type/state, DoorRecord.obj, ...). Without
    # it every inherited-field access is a "no member named" error. The warning
    # suppressions mirror ge007 so the shared sources compile identically; emcc is
    # Clang-based, so the Clang-only block (:1820) applies too (-ferror-limit=0,
    # -Wno-microsoft-anon-tag). -mno-ms-bitfields is intentionally OMITTED: it is an
    # x86-Windows layout toggle; wasm already uses the SysV/Itanium layout the port
    # requires (same as macOS/Linux, where the flag is a documented no-op).
    target_compile_options(ge007_web PRIVATE
        "-sUSE_SDL=2"
        "-msimd128"
        -Wall
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-unused-but-set-variable
        -Wno-missing-braces
        -Wno-parentheses
        -Wno-unknown-pragmas
        -Wno-implicit-function-declaration
        -Wno-missing-declarations
        -Wno-int-conversion
        -Wno-incompatible-pointer-types
        -Wno-empty-body
        -Wno-switch
        -fms-extensions
        -fno-strict-aliasing
        -Wno-macro-redefined
        -Wno-initializer-overrides
        -Wno-flexible-array-extensions
        -Wno-gnu-flexible-array-initializer
        -Wno-invalid-token-paste
        -Wno-microsoft-anon-tag
        -ferror-limit=0
    )
endif()

# ===========================================================================
# MGB64 App Shell (portable in-process ImGui launcher + overlay)
# Built as a separate C++/ObjC static library with clean flags (kept free of
# the C-only decomp flags on the ge007 target), linked into ge007. main() comes
# from src/app/main_app.cpp; main_pc.c renames its main to mgb64_headless_main.
# ===========================================================================
if(MGB64_APP)
    set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui)
    set(NFD_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/lib/nfd)

    set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )

    if(APPLE)
        set(NFD_PLATFORM_SOURCE ${NFD_DIR}/src/nfd_cocoa.m)
    elseif(WIN32)
        set(NFD_PLATFORM_SOURCE ${NFD_DIR}/src/nfd_win.cpp)
    else()
        set(NFD_PLATFORM_SOURCE ${NFD_DIR}/src/nfd_portal.cpp)
    endif()

    file(GLOB APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp)

    add_library(mgb64_app STATIC
        ${IMGUI_SOURCES}
        ${NFD_PLATFORM_SOURCE}
        ${APP_SOURCES}
    )
    target_compile_features(mgb64_app PRIVATE cxx_std_17)
    # GL_SILENCE_DEPRECATION: macOS deprecates the GL API but still ships it.
    # NOTE: do NOT add the engine's `include/` dir here — it contains a `math.h`
    # shim for the C decomp that shadows the system <math.h> and breaks C++/ObjC
    # system headers (ImGui stb_truetype `acos`, nfd `double_t`). The app lib
    # only needs the seam headers under src/platform + src/app.
    target_compile_definitions(mgb64_app PRIVATE MGB64_APP GL_SILENCE_DEPRECATION
        MGB64_APP_VERSION_STR="${MGB64_VER_STR}")
    # Keep the engine's broad source dirs OFF the app lib's include path: src/
    # contains headers (sched.h, byteswap.h) and include/ a math.h shim that
    # shadow the system angle-includes SDL/pthread/ImGui pull in. App<->engine
    # seam headers therefore live in the collision-free src/app/ dir.
    target_include_directories(mgb64_app PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${NFD_DIR}/include
        src/app
        lib/glad/include
    )
    # WebGPU launcher path: the app lib gets the in-house ImGui-on-WebGPU renderer
    # (gfx_webgpu_imgui) + the wgpu headers/lib, so the launcher UI + F1 overlay
    # can render through WebGPU. src/platform/fast3d is safe on the app include
    # path (fast3d seam headers only; NOT the engine's math.h shim in include/).
    if(MGB64_WEBGPU_BACKEND)
        target_sources(mgb64_app PRIVATE src/platform/fast3d/gfx_webgpu_imgui.cpp)
        target_include_directories(mgb64_app PRIVATE src/platform/fast3d)
        target_compile_definitions(mgb64_app PRIVATE MGB64_WEBGPU_BACKEND)
        target_link_libraries(mgb64_app PUBLIC webgpu)
    endif()
    if(SDL2_FOUND)
        target_link_libraries(mgb64_app PUBLIC SDL2::SDL2)
    elseif(SDL2_LIBRARIES)
        target_include_directories(mgb64_app PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(mgb64_app PUBLIC ${SDL2_LIBRARIES})
    endif()
    if(APPLE)
        target_link_libraries(mgb64_app PUBLIC
            ${OPENGL_LIB}
            "-framework AppKit"
            "-framework UniformTypeIdentifiers"
        )
    elseif(WIN32)
        # nfd_win.cpp uses COM + the Shell IFileOpenDialog; MinGW does not
        # auto-link these, so they must be explicit or the link fails.
        target_link_libraries(mgb64_app PUBLIC OpenGL::GL ole32 shell32 uuid)
    else()
        # Linux: GL + the nfd xdg-desktop-portal backend. D-Bus is REQUIRED --
        # nfd_portal.cpp hard-includes <dbus/dbus.h>, so fail loudly at configure
        # (install libdbus-1-dev) rather than with a confusing compile error.
        target_link_libraries(mgb64_app PUBLIC OpenGL::GL)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(DBUS REQUIRED dbus-1)
        target_include_directories(mgb64_app PRIVATE ${DBUS_INCLUDE_DIRS})
        target_link_libraries(mgb64_app PUBLIC ${DBUS_LIBRARIES})
    endif()

    # ge007's entry point now comes from the app shell.
    target_compile_definitions(ge007 PRIVATE MGB64_APP)
    target_link_libraries(ge007 mgb64_app)

    if(BUILD_TESTING)
        add_executable(test_arg_triage
            tests/test_arg_triage.cpp
            src/app/arg_triage.cpp)
        target_include_directories(test_arg_triage PRIVATE src/app)
        target_compile_features(test_arg_triage PRIVATE cxx_std_17)
        add_test(NAME arg_triage COMMAND test_arg_triage)

        # Pure argv -> LaunchIntent parser (AUDIT-0060 core), ROM-/SDL-free.
        # Links the single-sourced stage tables directly. The engine include
        # dirs are needed ONLY by cli_stage_tables.c (bondconstants.h enum
        # values) and are scoped to that one source: include/ carries the C
        # decomp's math.h shim, which shadows <math.h> and breaks C++ system
        # headers, so it must never reach the C++ TUs (same hazard the
        # mgb64_app block documents).
        add_executable(test_launch_intent
            tests/test_launch_intent.cpp
            src/app/launch_intent.cpp
            src/app/cli_stage_tables.c)
        target_include_directories(test_launch_intent PRIVATE src/app)
        set_source_files_properties(src/app/cli_stage_tables.c PROPERTIES
            INCLUDE_DIRECTORIES
            "${CMAKE_SOURCE_DIR}/include;${CMAKE_SOURCE_DIR}/include/PR;${CMAKE_SOURCE_DIR}/src;${CMAKE_SOURCE_DIR}/src/platform;${CMAKE_SOURCE_DIR}")
        target_compile_features(test_launch_intent PRIVATE cxx_std_17)
        add_test(NAME launch_intent COMMAND test_launch_intent)

        # Pure seed of a parsed LaunchIntent onto LauncherState (AUDIT-0060 E1).
        # launch_seed.cpp + LauncherState (ui_launcher.h -> engine_entry.h +
        # rom_validate.h, both plain C headers) are ImGui-/SDL-/ROM-free, so this
        # links tiny — no engine include dirs, no cli_stage_tables. ROM validation
        # + ensureInit ordering live in Launcher::seed and are covered process-side.
        add_executable(test_launcher_seed
            tests/test_launcher_seed.cpp
            src/app/launch_seed.cpp)
        target_include_directories(test_launcher_seed PRIVATE src/app)
        target_compile_features(test_launcher_seed PRIVATE cxx_std_17)
        add_test(NAME launcher_seed COMMAND test_launcher_seed)

        add_executable(test_rom_validate
            tests/test_rom_validate.cpp
            src/app/rom_validate.cpp)
        target_include_directories(test_rom_validate PRIVATE src/app)
        target_compile_features(test_rom_validate PRIVATE cxx_std_17)
        add_test(NAME rom_validate COMMAND test_rom_validate)

        # Keyboard-free ROM discovery (auto-scan + directory browser). rom_scan.cpp
        # is SDL-/ImGui-free precisely so this links tiny. The test harness uses
        # POSIX mkdtemp / 2-arg mkdir, so it is host-only (not a Windows-native
        # BUILD_TESTING build); rom_scan.cpp itself still cross-compiles for MinGW.
        if(NOT WIN32)
            add_executable(test_rom_scan
                tests/test_rom_scan.cpp
                src/app/rom_scan.cpp)
            target_include_directories(test_rom_scan PRIVATE src/app)
            target_compile_features(test_rom_scan PRIVATE cxx_std_17)
            add_test(NAME rom_scan COMMAND test_rom_scan)
        endif()

        # App-prefs value escaping (F1): a dropped/browsed filename with an
        # embedded newline must not inject an ini line. SDL-free core so this
        # links tiny and runs everywhere.
        add_executable(test_app_config
            tests/test_app_config.cpp
            src/app/app_config_escape.cpp)
        target_include_directories(test_app_config PRIVATE src/app)
        target_compile_features(test_app_config PRIVATE cxx_std_17)
        add_test(NAME app_config COMMAND test_app_config)

        # Launcher env-ownership authoritative reconciliation — ROM-free/SDL-free
        # unit test (AUDIT-0022): the pure core that makes applyModeEnv authoritative
        # over launcher-owned GE007_* keys so a re-enabled hatch or a deleted advanced
        # override is unset/restored across the Return-to-Launcher re-exec instead of
        # surviving stale. Covers hatch ON=>unset / OFF=>"0", advanced parse parity,
        # external-value capture+restore, idempotency, and the record round-trip.
        add_executable(test_env_ownership
            tests/test_env_ownership.cpp
            src/app/env_ownership.cpp)
        target_include_directories(test_env_ownership PRIVATE src/app)
        target_compile_features(test_env_ownership PRIVATE cxx_std_17)
        add_test(NAME env_ownership COMMAND test_env_ownership)

        # Settings staging layer (post-boot UX): mid-game edits stay on a working
        # copy until Apply. Links only the registry + config layers (no SDL), with
        # the engine include dirs so <ultra64.h> resolves for the C sources.
        add_executable(test_config_staging
            tests/test_config_staging.c
            src/platform/config_schema.c
            src/platform/config_pc.c
            src/platform/settings.c
            src/platform/savedir.c)
        target_include_directories(test_config_staging PRIVATE
            src/app src/platform include include/PR .)
        add_test(NAME config_staging COMMAND test_config_staging)

        # AUDIT-0055: a transient GE007_* env override must never erase/alter the
        # persisted config value. Two-launch persistence matrix (all five setting
        # types) plus the staging live-preview containment, driven through the real
        # configInit/env-override/configSave/reload lifecycle against a temp
        # savedir. ROM-free; same registry+config link set as config_staging.
        add_executable(test_config_env_shadow
            tests/test_config_env_shadow.c
            src/platform/config_schema.c
            src/platform/config_pc.c
            src/platform/settings.c
            src/platform/savedir.c)
        target_include_directories(test_config_env_shadow PRIVATE
            src/app src/platform include include/PR .)
        add_test(NAME config_env_shadow COMMAND test_config_env_shadow)

        # AUDIT-0036 core: the config-save boundary must report OK / SUPPRESSED /
        # FAILED (configSaveResult), and the 0/1 configSave()/mgb_config_save()
        # wrappers plus the tri-state thread-up (configStagingApply /
        # mgb_config_save_result) must keep their contracts. ROM-free; same
        # registry+config link set as config_staging / config_env_shadow.
        add_executable(test_config_save_result
            tests/test_config_save_result.c
            src/platform/config_schema.c
            src/platform/config_pc.c
            src/platform/settings.c
            src/platform/savedir.c)
        target_include_directories(test_config_save_result PRIVATE
            src/app src/platform include include/PR .)
        add_test(NAME config_save_result COMMAND test_config_save_result)

        # Pure tag-parse + semver-compare logic for the update check (no SDL, no
        # network): update_check_core.cpp is SDL-free precisely so this links tiny.
        add_executable(test_update_check
            tests/test_update_check.cpp
            src/app/update_check_core.cpp)
        target_include_directories(test_update_check PRIVATE src/app)
        target_compile_features(test_update_check PRIVATE cxx_std_17)
        add_test(NAME update_check COMMAND test_update_check)
    endif()
endif()

# ===========================================================================
# macOS App Bundle (optional — builds game as static library for Swift shell)
# Usage: cmake -B build-macos -DMACOS_APP_BUNDLE=ON
# ===========================================================================
option(MACOS_APP_BUNDLE "Build as macOS .app bundle (static library + Swift shell)" OFF)

if(MACOS_APP_BUNDLE AND APPLE)
    # Set architecture and deployment target BEFORE creating the library target
    # so CMake applies them at generation time.
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES OR CMAKE_OSX_ARCHITECTURES STREQUAL "")
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()

    # Collect all C sources for the static library (same as executable, minus main)
    set(BRIDGE_SOURCES
        macos/Sources/GameBridge.c
    )

    add_library(ge007_lib STATIC
        ${GAME_SOURCES}
        ${SYSTEM_SOURCES}
        ${ASSET_SOURCES}
        ${PLATFORM_SOURCES}
        ${FAST3D_SOURCES}
        ${GLAD_SOURCES}
        ${LIBULTRA_AUDIO_SOURCES}
        ${LIBULTRARE_AUDIO_SOURCES}
        ${BRIDGE_SOURCES}
    )

    # The global add_definitions() (NONMATCHING, NATIVE_PORT, etc.) apply
    # automatically. We only need to add the app bundle flag.
    target_compile_definitions(ge007_lib PRIVATE
        MACOS_APP_BUNDLE
    )
    if(PORT_SOUNDPLAYER_REAL)
        target_compile_definitions(ge007_lib PRIVATE PORT_SOUNDPLAYER_REAL)
    endif()

    # WebGPU backend in the .app static-lib path (propagates the pinned prebuilt
    # + frameworks as usage requirements to the final Swift-shell link).
    if(MGB64_WEBGPU_BACKEND)
        target_compile_definitions(ge007_lib PRIVATE MGB64_WEBGPU_BACKEND)
        # Plain signature to match the other ge007_lib link calls.
        target_link_libraries(ge007_lib webgpu)
    endif()

    target_include_directories(ge007_lib PRIVATE
        include
        include/PR
        src
        src/game
        src/platform
        src/platform/fast3d
        src/libultra/audio
        assets
        lib/glad/include
        lib/stb
        lib/cgltf   # decor_assets.c #includes cgltf.h; ge007/ge007_web already list this — ge007_lib (macOS universal build via build_universal.sh) was missing it, breaking the macos-release universal-library step
        macos/Sources
        .
    )

    # Same compiler flags as the main executable
    target_compile_options(ge007_lib PRIVATE
        -Wall
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-unused-but-set-variable
        -Wno-missing-braces
        -Wno-parentheses
        -Wno-unknown-pragmas
        -Wno-implicit-function-declaration
        -Wno-missing-declarations
        -Wno-int-conversion
        -Wno-incompatible-pointer-types
        -Wno-empty-body
        -Wno-switch
        -fms-extensions
        # See the ge007 target above for both of these: N64-matching bit-field
        # layout on every platform (Windows/MinGW defaults to -mms-bitfields),
        # and decomp-port pointer punning.
        ${MGB64_MS_BITFIELD_FLAG}
        -fno-strict-aliasing
    )

    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(ge007_lib PRIVATE
            -Wno-macro-redefined
            -Wno-initializer-overrides
            -Wno-flexible-array-extensions
            -Wno-gnu-flexible-array-initializer
            -Wno-invalid-token-paste
            -Wno-microsoft-anon-tag
            -ferror-limit=0
        )
    endif()

    target_link_libraries(ge007_lib
        ${OPENGL_LIB}
        m
    )
    if(SDL2_FOUND)
        target_link_libraries(ge007_lib SDL2::SDL2)
    elseif(SDL2_LIBRARIES)
        target_include_directories(ge007_lib PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(ge007_lib ${SDL2_LIBRARIES})
    endif()

    message(STATUS "macOS App Bundle: ON")
    message(STATUS "  Library target: ge007_lib")
    message(STATUS "  Architectures: ${CMAKE_OSX_ARCHITECTURES}")
    message(STATUS "  Deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    message(STATUS "  Link the Swift app against libge007_lib.a")
endif()
