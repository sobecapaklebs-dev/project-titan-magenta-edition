# Project Status

**MGB64 is an experimental but genuinely playable native port.** The full
single-player campaign boots, plays, and reaches mission-complete across all 20
levels from a clean checkout plus your own ROM — this is well past a proof of
principle. It is still experimental: not a one-command, plug-and-play consumer
release, and not a 1:1 replacement for the original on real hardware. This
document is the honest current state so you know what to expect and where help is
most valuable.

## What works

- A large portion of the game is **decompiled** to readable C/assembly:
  rendering, character AI, combat, level logic, menus, and audio systems.
- **The native port builds and runs from a clean checkout + your own ROM.**
  `cmake -B build && cmake --build build` produces `ge007`; running it loads
  your ROM, decodes its audio, brings up the OpenGL renderer, and enters the
  game loop. Verified end-to-end on macOS/arm64. The repository also wires a
  Linux/GCC native build plus ROM-free CTest suite through local preflight and
  source-archive smoke. Hosted GitHub Actions is not a required release gate.
- The port is **asset-free**: no ROM media is compiled into the binary. All bulk
  game data — textures, audio, animation frames, fonts, the Rareware logo — is
  read from *your* ROM at runtime (`rom_io.c`) and served through the
  stub/segment layer (`src/platform/asset_stubs.c`, `segment_stubs.c`). The
  repository ships only first-party + code-coupled source (display lists,
  descriptor/offset tables, model-header glue), never ROM media.
- **Save persistence has smoke coverage:** deterministic solo completions are
  written to two save folders across separate processes, then reloaded from disk
  after a final process restart (`tools/save_persistence_check.sh`). The trace
  writer now emits the frontend save summary even before a live player exists,
  so this gate can validate title/menu reload state directly instead of relying
  only on logs and EEPROM bytes. `tools/dam_mission_flow_smoke.sh` separately
  proves that Dam's scripted mission-success path writes folder 0 Dam/Agent
  completion and that a fresh process reload observes it from the generated
  EEPROM.
- **Direct gameplay input has smoke coverage:** `tools/playability_smoke.sh`
  direct-boots levels deterministically, applies real gameplay stick input, and
  requires movement records, horizontal player displacement, clean watch state,
  zero assertions, screenshot-health-clean captures, and render-health-clean
  traces. It writes per-attempt audit JSON plus a top-level `summary.json` so
  local ROM-backed playability evidence can be reviewed without scraping logs.
- **Modern minimap coverage is all-stage and opt-out-safe:** `tools/minimap_smoke.sh`
  direct-boots every solo stage by default, audits STAN cache readiness,
  objective pins derived from setup criteria, overlay draw summaries, screenshot
  health, render health, and then repeats disabled-mode parity checks to prove
  `Input.MinimapEnabled=0` queues no snapshots and draws no overlay while the
  cache remains deterministic.
- **Native playability guard orchestration exists:** `tools/native_playability_regression_suite.sh`
  is the local long-running convenience lane for build/CTest plus playability,
  structured campaign route contracts/input traversal, Dam progression, Surface
  II final flow, combat/guard/knife behavior, renderer parity, MP split-screen,
  save persistence, and minimap coverage. The individual gates remain the source
  of truth for focused triage.
- **2-player split-screen multiplayer is wired (input + launch + aim):** the
  native port opens every connected pad into its own player slot, fills
  `data[1..3]` so `joyGetControllerCount() >= 2` unblocks the MP menus, direct-boots
  a deterministic split-screen match via `--multiplayer/--players N/--mp-stage/
  --scenario`, and routes aim per player (mouse-look → P1, pad `k` → player `k`).
  `tools/mp_smoke.sh` is the 2-player measurement lane: it boots a deathmatch,
  drives a scripted player-1 input window, and asserts the two framebuffer halves
  are measurably dissimilar so a duplicated-camera bug fails. What is **proven**
  today: the 2-player lane is **green** — boot, two distinct viewports (~97%
  dissimilar halves), render-health clean, zero crashes; and 4-player boots and
  renders distinct viewports in the same smoke window. What is **pending**:
  sustained-load frame budget, the higher-risk 3-player asymmetric split, and a
  full end-of-round scoreboard run are not yet validated.
- **Recent playability fixes are landed and traceable:** Dam's intro truck now
  binds its authored vehicle AI path and target speed, and vehicle geometry uses
  the corrected native vector path so the truck body/wheels render together.
  Bunker 1's full-game-boot movement regression is also fixed and documented:
  the native player gait/head model pointer is initialized and validated, the
  out-of-line native `field_5C0` gait-frame mirror is initialized/synchronized,
  bad gait animation frame state is repaired before `bhead` ticking, and native
  immediate play-speed changes keep `animrate` synchronized. This prevents the
  bad full-boot signature where Bond is stuck at spawn and logs repeat
  `[ANIM_FRAME*_GUARD] ... chr=0x0 ... frame=-...`. The related Bunker
  datathief/debug-dump crash is also guarded: live dumps validate prop/model/dyn
  pointers by default and only read bone matrices or root rwdata behind explicit
  opt-in flags. Keep these guarded with `tools/playability_smoke.sh --level 9
  --frames 420 --input-window 120:100 --pattern forward --no-build`, the no-arg
  selector Bunker probe, the forced-authored-intro Bunker probe, the
  `GE007_DIAG_POISON_BHEAD_FRAME=1` poisoned-frame recovery probe, and the
  datathief auto-equip/debug-dump probe in
  [INSTRUMENTATION.md](INSTRUMENTATION.md#bunker-1-full-boot-gaitdebug-dump-regressions).
  Transparent glass no longer disappears while collision remains active: the
  native renderer handles secondary room alpha plus prop-type glass material
  paths, and glass bullet-crack decals stay surface-aligned instead of vanishing
  edge-on. The maintained probes live in
  [INSTRUMENTATION.md](INSTRUMENTATION.md): the vehicle probe expects Dam truck
  `obj=279`, AI list `0x040A`, `path_id=7`, and a moving `target_pad=312`
  startup; the glass probe audits secondary alpha material classification and
  glass draw ranges. The latest local MP timer acceptance run reached the forced
  `60/60` tick match boundary with about `98%` split-half dissimilarity and zero
  render-health failures; scoreboard/results transition proof remains open.
- **The recent texture regression class is documented and guarded:** Cradle,
  Dam, and Surface exposed several renderer mistakes that looked similar by eye:
  room-nearest overrides, N64 filter-footprint scaling against the modern
  framebuffer, decoded row-pitch/cache identity drift, stale `G_SETTEX` tile
  state, and fog-depth false positives. The fixes and the keep-it-fixed
  checklist are recorded in
  [RENDERING_REGRESSION_NOTES.md](RENDERING_REGRESSION_NOTES.md), and the broad
  native gate is `tools/playability_smoke.sh --all`, including a contact sheet
  for manual review.
- **Modern display controls are available through the native settings schema:**
  window mode, display selection, fullscreen mode sizing, VSync, frame cap,
  render scale, MSAA, gamma, retro filtering, and gameplay FOV are configurable
  while defaulting back to the original 4:3-style presentation. See
  [PORTING_AND_EXPANSION.md](PORTING_AND_EXPANSION.md).
- **ROM-vs-native comparison tooling exists for targeted parity work:**
  `docs/ROM_COMPARISON.md` documents route specs, native traces, optional
  instrumented ares stock traces, movement/intro comparators, and structured
  JSON artifacts. The built-in Dam routes cover forward/strafe scalar movement
  speed dynamics plus selected-camera and timer-aligned swirl/Bond-animation
  intro checks. Captured traces/screenshots remain local ROM-derived artifacts
  and are not shipped.
- **The macOS `.app` shipped on releases is built from source by**
  `macos/Scripts/build_gl_app.sh` (the in-process ImGui bundler), which assembles
  `build-macos-app/MGB64.app` and keeps it asset-free
  (`macos/Scripts/verify_asset_free.sh build-macos-app/MGB64.app`). The legacy
  Swift/AppKit `macos/Scripts/build_app_bundle.sh` remains for reference.

## Remaining work

- **N64 ROM (byte-matching) build:** the native port is the supported target.
  Rebuilding the original N64 ROM from source additionally requires local,
  external IDO static-recompilation tooling plus user-supplied SGI IDO/IRIX
  compiler files for the matching toolchain; neither the recompilation source
  nor the proprietary compiler inputs are redistributed here. It also requires
  the ROM-derived data tables to be laid out at their ROM addresses; wiring the
  extracted `.bin` of the animation / image tables into the N64 link (the repo's
  existing `.incbin` pattern, as used by `music.s`/`ob_seg.s`) is the main
  code/data-link task there.
- **Release hygiene / SDK replacement:** the tree no longer tracks proprietary
  SDK notice text, but it still contains inventoried SDK/libultra-lineage
  compatibility material for the in-progress matching target. This remains the
  largest conservative provenance area, so do not describe the repository as
  fully clean-room. The current native CMake build no longer compiles any
  `src/libultra/audio/*.c` or
  `src/libultrare/audio/*.c` sources. The top-level compatibility headers
  `include/{assert,bstring,limits,math,sgidefs,stddef,stdlib,svr4_math}.h` and
  `include/PR/{R4300,abi,gbi,gu,libaudio,mbi,os,os_internal,rcp,region,ultratypes}.h` have
  been replaced clean-room, as have internal libultra helper headers
  `src/libultra/{audio/synthInternals.h,gu/guint.h,io/viint.h}`.
  The `NATIVE_PORT` branch of `include/PR/os.h` now forwards to the clean native
  platform surface instead of redefining OS/address macros. The native `include/PR/R4300.h`
  address macros are now guarded as fallback definitions so they do not override
  the native platform address conversions. Most matching-target GU C helpers
  `src/libultra/gu/{align,cosf,coss,lookat,lookatref,mtxutil,normalize,ortho,perspective,rotate,scale,sinf,sins,translate}.c`
  and `src/libultra/io/viblack.c` are now clean-room compatibility sources. The
  native CMake targets no longer expose the broad `src/libultra/`,
  `src/libultra/gu/`, or `src/libultrare/audio/` include directories; native
  audio compatibility keeps only the narrow `src/libultra/audio/` include path
  for the clean-room `synthInternals.h` / `seqp.h` surface.
  The former split libaudio helper/wrapper files under `src/libultra/audio/` are no
  longer tracked as standalone sources. Native and matching-target builds share
  the project-owned implementation in `src/platform/audio_compat.c`; the
  matching-target makefile reaches it through
  `src/libultra/audio/clean_compat.c` instead of compiling historical SDK
  libaudio implementation files or unbuilt duplicate wrappers.
  The matching-target linker scripts likewise place only
  `src/libultra/audio/clean_compat.o` for libaudio sections; historical
  SDK/Rare libaudio object references are release-guarded against returning.
  The native SDK/demo-lineage support-code audit is recorded in
  [PROVENANCE_AUDIT.md](PROVENANCE_AUDIT.md). The remaining tracked
  SDK-shaped compatibility paths under `include/PR/`, `src/libultra/`, and
  `src/libultrare/` are explicitly inventoried and guarded by
  `../tools/check_sdk_inventory.py`, so the public provenance and libaudio
  linker surfaces cannot grow without a deliberate inventory/docs update.
- **Renderer/audio accuracy** and assorted gameplay parity vs. original
  hardware (see below).
- **Signed/notarized macOS distribution:** tagged releases already ship prebuilt
  artifacts on all three platforms — macOS `.app` (zip), Windows portable `.zip`,
  and Linux AppImage/`.tar.gz` (see [RELEASING.md](RELEASING.md)), each asset-free.
  What remains deferred is the macOS `.app`'s Developer ID signing, notarization,
  a controlled SDL2 deployment target, and DMG polish: it currently ships unsigned
  (right-click → Open to pass Gatekeeper) and links the builder's SDL2 dylib.

## Known issues

- Graphical inaccuracies and occasional rendering glitches versus original
  hardware. The renderer now has strict render-health counters, screenshot
  health gates, and a small renderer parity scene lane, but visual accuracy is
  still not claimed as hardware-perfect. See
  [RENDERING_REGRESSION_NOTES.md](RENDERING_REGRESSION_NOTES.md) for the current
  texture/glass/fog failure taxonomy and validation path.
- Authored level intro parity is still incomplete across the full game, but Dam
  now has local ROM-vs-native selected-camera/static-camera coverage plus
  timer-aligned swirl/Bond-animation coverage and native actor/render/held-item
  trace auditing. A local native intro-census lane can now sweep direct-boot
  stages for active intro cameras, decoded swirl setup hashes, Bond
  render/animation coverage, animation header hashes, screenshot health, and
  render-health counters.
- Some gameplay/behavior differences and occasional instability or crashes.
  Movement-speed parity has targeted ROM-backed Dam coverage and all-level
  deterministic native playability smoke coverage. Dam now has an active
  progression wrapper that composes spawn movement, real objective criteria
  advancement, and mission-report return, Surface II has a final-flow guard,
  and `tools/campaign_route_smoke.sh` provides a JSON route-contract layer for
  mission assertions plus fragile-set input traversal across Dam, Facility,
  Surface 1, Surface II, Bunker 1, Frigate, Statue, Train, Control, Caverns,
  and Cradle.
  Native route specs now have declarative `input_segments` for controller
  windows, which compile to the existing `GE007_AUTO_*` input hooks and reduce
  raw env-string drift while promoted routes grow. The traversal routes also
  assert setup-backed waypoint proximity against real stage pads, so their
  controller movement is tied to authored level geography rather than only
  relative displacement. `campaign_route_smoke.py` now also reports a
  conservative capability tier for each route (`T0` boot/trace through `T5`
  stage loop), plus opened doors, collect types, keyflags, report/reload frames,
  stock-input status, and aggregate tier counts in the top-level summary. This
  makes the durable baseline and future scout promotion thresholds explicit
  instead of treating route count alone as progress. The current corrected
  default route run passes 34/34 with tier counts `T1=17`, `T2=9`, `T3=3`,
  `T4=2`, and `T5=3`. `tools/route_target_reach.py` now gives exploratory
  route batches a reusable setup-target reach diagnostic, including same-floor
  and vertical separation, so failed scouts can be bounded without pretending
  distance alone is gameplay progress. Dam additionally has a native-only multi-waypoint
  traversal lane that reaches the later authored pad cluster around pads
  `293`/`287`/`288`, plus a scripted mission composite that runs that
  controller lane before proving Dam objective-vector progression,
  mission-report return, and save/reload persistence in one route; Surface 1
  now has a native-only snowfield lane that
  reaches pads `80`/`79`/`77`; Surface II now has a native-only outdoor lane
  from stock spawn through pads `30`/`71`/`70`/`69`/`68` and broad late pad
  `78`, with more than 10,500 horizontal units of reach; Frigate has a
  native-only deck lane that reaches
  pads `176`/`153`/`152`/`151`/`150`/`149`/`144`/`60`/`145` and more than 2,800
  horizontal units; Statue has a native-only park/lower-cluster lane
  that reaches pads `222`/`214`/`207`/`208`, plus a stock-spawn door route that
  backs to door object `183`/pad `6`, opens it with a single real `B` press, and
  proves `door allow`, `0->1`, displacement, and finish-open trace evidence
  without warp, force, or stage-state automation; Train has a native-only narrow-car
  lane that reaches room-3 boundpads `12`/`14`; and Cradle has a native-only
  multi-waypoint traversal lane that reaches pad `121` plus later pad-cluster
  targets, plus a stock-spawn body-armour pickup route that reaches object
  `115` at pad `124` and proves the normal object type `21` collect/free path,
  all without warp, force, item injection, or stage-state automation. Bunker 1 now also
  has stock-spawn input-interaction routes that open the first corridor door,
  reach the next-room prop cluster, collect an object through normal
  interact/door/collect paths, and in the longer chain turn to second door
  object `138` near pad `14` and open it through another real `B` interaction,
  all without warp, force, item injection, or stage-state automation. Facility
  now also has a stock-spawn input-interaction
  route that reaches the bathroom door cluster, opens object `159` through real
  `B` interaction, and pushes deeper than 1,200 horizontal units without warp,
  force, or stage-state automation; a second Facility stock-spawn route now
  chains that into a real object-`155` door open near pad `75`, again without
  warp, force, or stage-state automation. Control and Caverns now also have
  stock-spawn input-interaction routes that open door object `144` through real `B`
  interaction and push into later room/pad clusters without warp, force, or
  stage-state automation. Scripted routes now use
  declarative `scripted_events` for deterministic setup hooks such as warps,
  face targets, forced pose, tag damage, stage flags, guard AI, item equip,
  mission end, and title exit, with raw-env collision validation. They can also
  assert `setup_target_milestones`, which tie deterministic placements to real
  setup dump rows such as Facility door object `158`/pad `77` and Surface 1 key
  pad `17`. The suite also carries scripted contracts for Dam mission-report,
  the Dam native mission composite, Surface II input-only outdoor traversal,
  and Surface II final-exit save/reload persistence, Dam guard pressure,
  Dam player-fire guard damage,
  Bunker 1 datathief equipment/debug-dump stability plus stock-spawn
  door/collect and two-door/collect interaction, Control/Caverns stock-spawn
  door traversal,
  Facility door open/reverse interaction, Facility stock-spawn two-door
  interaction, and Surface 1 proximity key pickup.
  Focused combat gates cover
  active guard fire, hidden-guard no-phantom-fire/no-damage
  behavior, a throwing-knife guard impact crash path, and throwing-knife flight
  SFX submissions. The default objective/report/exit/equipment/door/pickup/combat
  contracts are still scripted, and the input routes are traversal or focused
  interaction slices, not full objective-completion navigation.
  Full mission routes with broader menus, route-driven combat encounters,
  organic pickup paths, route-driven doors, alarms, endings, mission report, and
  persistence from organic completion still need reference-backed expansion.
  The latest Surface 1 stock-spawn key-route scouts did not promote: 36
  controller-pattern captures stayed at least about 3,652 horizontal units from
  the key area and never emitted key collect/keyflag evidence.
  Bunker 1's promoted two-door/collect chain improves stock-spawn interaction
  depth, but the key/objective scout boundary remains: the first continuation
  pass found nearby second-door object `138` and a later angle pass promoted that
  door, while key pad `8` still did not emit key collect/keyflag evidence. A
  later key-return scout was stopped after 34 logs/33 summaries with no
  `objtype=4` pickup, no nonzero keyflag, and no key-door evidence, so it was
  not promoted. A follow-up 12-route lower-floor branch scout also did not
  promote: all routes preserved the existing two-door/collect evidence but
  emitted no key/keyflag/key-door rows, and the best same-floor key approach
  remained about 492 horizontal units from key object `86`/pad `8`.
  Surface II now has a long stock-spawn traversal lane plus a separate scripted
  final-exit persistence contract, but no organic bridge from spawn through
  objective setup to the final silo/report/reload flow yet.
  Follow-up Surface II bridge scouts passed process/render audits but did not
  promote: the best candidates stayed roughly 25,120 horizontal units from final
  pad `289` and remained closest to the spawn-side pad cluster.
  `tools/soak_stability.sh` is the headless deterministic measurement path for
  crash/render-health regressions over long per-stage runs (no numeric
  stability budget is claimed yet); `tools/asan_smoke.sh` adds a report-only
  ASan/UBSan lane.
- Audio is functional and the SFX mapping/owner-slot path has been validated.
  Native music now follows ABI1 little-endian sample-lane ordering for the
  envmixer and custom pole-filter paths, with additive aux-return mixing. It
  still needs hardware/reference parity work for final reverb balance and any
  remaining command-level edge cases.
- This is **not** a 1:1 replacement for the original ROM on real hardware.

## Good first issues

- Wire the extracted animation/image-table `.bin` into the **N64 ROM build** via
  the `.incbin` pattern so `make` rebuilds the ROM from a clean clone + ROM.
- Replace one narrow SDK compatibility surface with clean-room declarations or
  implementation, starting with native-facing graphics/OS/audio declarations.
- Improve renderer accuracy for a specific effect (compare against a reference
  emulator).
- Expand organic menu/mission-flow validation beyond the current deterministic
  multi-folder EEPROM and scripted Dam mission-success persistence checks.
- Port build ergonomics: local build coverage on more platforms, packaging.
- Documentation: expand build notes for your platform.

For the fuller contributor roadmap, see [../ROADMAP.md](../ROADMAP.md). If you
want to help but aren't sure where to start, open a discussion or a draft PR and
ask. See [../CONTRIBUTING.md](../CONTRIBUTING.md).
