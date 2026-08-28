#!/usr/bin/env python3
"""
WEB-043 guard — controls-documentation drift gate.

Player-facing key documentation is hand-duplicated across several surfaces and
has already drifted once into a P0 (the F1/F10 / WEB-001 incident). This gate
extracts the ground truth from code and asserts the web landing page's controls
surfaces still agree with it. It CHECKS docs — it does not rewrite them. On a
real mismatch it fails with the surface, the row, expected-vs-found, and where
to fix.

Ground-truth sources (authoritative):
  * src/platform/input_bindings.c  — kDefault/kLabel (keyboard) and
    kGpDefault/kGpLabel (gamepad) default binding tables.
  * src/platform/platform_sdl.c    — the hardcoded extras documented in the H
    help text: crouch (SDLK_c/LCTRL/RCTRL), watch (SDLK_ESCAPE/SDLK_TAB),
    mute (SDLK_m), help (SDLK_h), and the dev-only F-keys (F1/F2/F3..F12) that
    are gated behind pcDevHotkeysEnabled() (GE007_DEV_HOTKEYS).

Documentation surfaces checked:
  * web/index.html — the Controls <dl> rows (Movement/Combat/System/Gamepad),
    the .overlay-note paragraph, and the #overlay-hint div.
  * src/platform/platform_sdl.c — the `#ifdef __EMSCRIPTEN__` arm of the H-help
    "=== CONTROLS ===" printf (the console help a WEB player actually sees):
    F-key lock only. The native `#else` arm is exempt — it legitimately
    advertises F1 (settings overlay) / F10 (FPS toggle), which exist there.

What this gate asserts:
  1. F-KEY REGRESSION LOCK (WEB-001): no player-facing surface names F1..F12.
     The dev keys are gated behind GE007_DEV_HOTKEYS and must never be
     re-advertised. On the page, HTML comments (the "do NOT re-advertise"
     guidance block), <script> and <style> are stripped before the scan so the
     guidance itself is never flagged. The lock also runs over the string
     literals of the __EMSCRIPTEN__ H-help printf (C comments exempt; native
     #else arm exempt). Also verifies the code still dev-gates F1/F2/F3..F12,
     so the premise of the lock (F-keys are not player controls) still holds.
  2. High-value rows: the page's Crouch/Watch/Mute/Help rows name the exact
     keys the engine actually compares (C, Esc+Tab, M, H), and each of those
     keys is still present in the platform_sdl.c KEYDOWN handler.
  3. Keyboard bindings: Move (WASD), Fire (L Shift), Aim (R Shift), Reload (R),
     Lean (Q/E) on the page match kDefault; Use/interact (F) is anchored on the
     IB_RELOAD alternate documented in input_bindings.c.
  4. Gamepad defaults: Fire/Aim (RT/LT), Jump (A), Reload/Use (B),
     Next weapon (Y), Prev weapon (R3), Crouch (L3), Watch/pause (Start) on the
     page match kGpDefault.
  5. overlay-note names Esc/Tab/M/H; overlay-hint names Esc/M/H.

Out of scope (deliberately, to keep the false-positive rate low — robustness
over cleverness):
  * Exhaustive reverse mapping of *every* token the page prints back to code.
    Only the curated rows above plus the F-key lock are enforced. A newly added
    IB_/GB_ action is not auto-required on the page.
  * Free-form prose in the "About this demo" section.
  * Mouse/scroll bindings (not keyed in the binding registry).
  * The H-help body rows (WASD/Reload/Crouch/... lines) are not row-checked
    against kDefault — only the F-key lock applies to the __EMSCRIPTEN__ arm,
    and the native #else arm is exempt entirely (it names F1/F10 by design).

ROM-free, source-only, fast. Exits 0 on success, 1 on any drift, 2 on a
structural parse failure (a source/surface it could not read). `--self-test`
seeds drifts into throwaway copies and proves the gate flips red (negative
control); it exits 0 only when the pristine tree passes AND every seed fails.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
import tempfile
from pathlib import Path

DEFAULT_REPO_ROOT = Path(__file__).resolve().parents[2]

INDEX_REL = Path("web/index.html")
BINDINGS_REL = Path("src/platform/input_bindings.c")
SDL_REL = Path("src/platform/platform_sdl.c")

# Scancode enum suffix (input_bindings.c kDefault) -> the token the page prints.
KB_SCAN_TOKEN = {
    "W": "W", "A": "A", "S": "S", "D": "D",
    "R": "R", "Q": "Q", "E": "E",
    "LSHIFT": "L Shift", "RSHIFT": "R Shift",
}

# kGpDefault binding expression -> the abbreviation the page prints. Keyed on a
# canonical extracted from the C expression (see gp_token()).
GP_TOKEN = {
    "RT": "RT", "LT": "LT",
    "A": "A", "B": "B", "X": "X", "Y": "Y",
    "R3": "R3", "L3": "L3", "START": "Start",
    "RB": "Right Bumper", "LB": "Left Bumper",
}


# --------------------------------------------------------------------------- #
# HTML helpers
# --------------------------------------------------------------------------- #
def strip_noise(html: str) -> str:
    """Remove HTML comments, <script>, and <style> so only rendered, player-
    facing markup remains. The WEB-001 guidance comment names F-keys on purpose;
    stripping comments keeps it from tripping the F-key lock."""
    html = re.sub(r"<!--.*?-->", "", html, flags=re.DOTALL)
    html = re.sub(r"<script\b.*?</script>", "", html, flags=re.DOTALL | re.IGNORECASE)
    html = re.sub(r"<style\b.*?</style>", "", html, flags=re.DOTALL | re.IGNORECASE)
    return html


def norm(fragment: str) -> str:
    """Strip tags + collapse whitespace/entities so key tokens compare cleanly."""
    text = re.sub(r"<[^>]+>", " ", fragment)
    text = text.replace("&nbsp;", " ").replace("\xa0", " ").replace("&amp;", "&")
    return re.sub(r"\s+", " ", text).strip()


def has_token(value: str, token: str) -> bool:
    """Whole-word (case-sensitive) match for short key tokens; substring for
    multi-word tokens like 'L Shift'. Keys are printed uppercase on the page and
    derived uppercase from code, so case-sensitivity distinguishes e.g. M/m."""
    if " " in token:
        return token in value
    return re.search(r"\b" + re.escape(token) + r"\b", value) is not None


# --------------------------------------------------------------------------- #
# Ground-truth parsers
# --------------------------------------------------------------------------- #
def parse_kdefault(src: str) -> dict[str, str]:
    """input_bindings.c kDefault -> {IB_ACTION: SCANCODE_SUFFIX}."""
    m = re.search(
        r"static\s+const\s+int\s+kDefault\s*\[[^\]]*\]\s*=\s*\{(.*?)\};",
        src, re.DOTALL,
    )
    if not m:
        return {}
    out: dict[str, str] = {}
    for scan, action in re.findall(
        r"SDL_SCANCODE_(\w+)\s*,\s*//\s*(IB_\w+)", m.group(1)
    ):
        out[action] = scan
    return out


def gp_token(expr: str) -> str | None:
    """Canonicalize a kGpDefault C binding expression to a page abbreviation."""
    if "TRIGGERRIGHT" in expr:
        return "RT"
    if "TRIGGERLEFT" in expr:
        return "LT"
    bm = re.search(r"SDL_CONTROLLER_BUTTON_(\w+)", expr)
    if not bm:
        return None
    name = bm.group(1)
    return {
        "A": "A", "B": "B", "X": "X", "Y": "Y",
        "RIGHTSTICK": "R3", "LEFTSTICK": "L3",
        "START": "START",
        "RIGHTSHOULDER": "RB", "LEFTSHOULDER": "LB",
    }.get(name)


def parse_kgpdefault(src: str) -> dict[str, str]:
    """input_bindings.c kGpDefault -> {GB_ACTION: canonical token}."""
    m = re.search(
        r"static\s+const\s+int\s+kGpDefault\s*\[[^\]]*\]\s*=\s*\{(.*?)\};",
        src, re.DOTALL,
    )
    if not m:
        return {}
    out: dict[str, str] = {}
    for expr, action in re.findall(r"(.+?),\s*/\*\s*(GB_\w+)\s*\*/", m.group(1)):
        tok = gp_token(expr)
        if tok is not None:
            out[action] = tok
    return out


def sym_compared(src: str, sdlk: str) -> bool:
    """True if the KEYDOWN handler compares keysym.sym against `sdlk`."""
    return re.search(r"sym\s*==\s*" + re.escape(sdlk) + r"\b", src) is not None


def extract_web_help_text(sdl_src: str) -> str | None:
    """Isolate the `#ifdef __EMSCRIPTEN__` arm of the H-help printf (anchored on
    its "=== CONTROLS ===" literal) and return only its C string literals — the
    exact console text a web player sees. C comments are excluded by
    construction; the native `#else` arm is deliberately NOT returned (it
    legitimately advertises F1/F10, which exist on native). Returns None when
    the arm cannot be isolated unambiguously (structural failure — fail loud
    rather than silently skipping the surface)."""
    arms: list[str] = []
    for m in re.finditer(r"#ifdef\s+__EMSCRIPTEN__", sdl_src):
        rest = sdl_src[m.end():]
        stop = re.search(r"^\s*#\s*(?:else|endif)\b", rest, re.MULTILINE)
        arm = rest[: stop.start()] if stop else rest
        if "=== CONTROLS ===" in arm:
            arms.append(arm)
    if len(arms) != 1:
        return None
    literals = re.findall(r'"((?:[^"\\]|\\.)*)"', arms[0])
    return "\n".join(literals)


# --------------------------------------------------------------------------- #
# Page surface extraction
# --------------------------------------------------------------------------- #
def extract_surfaces(html: str) -> tuple[dict[str, dict[str, str]], str, str]:
    """Return (controls sections {Title:{label:value}}, overlay_note, overlay_hint)."""
    visible = strip_noise(html)

    sections: dict[str, dict[str, str]] = {}
    cm = re.search(r'<div class="controls"[^>]*>(.*?)</details>', visible, re.DOTALL)
    if cm:
        for sec in re.findall(r"<section[^>]*>(.*?)</section>", cm.group(1), re.DOTALL):
            hm = re.search(r"<h3>(.*?)</h3>", sec, re.DOTALL)
            title = norm(hm.group(1)) if hm else "?"
            rows: dict[str, str] = {}
            for dt, dd in re.findall(
                r"<dt>(.*?)</dt>\s*<dd>(.*?)</dd>", sec, re.DOTALL
            ):
                rows[norm(dt)] = norm(dd)
            sections[title] = rows

    nm = re.search(r'<p class="overlay-note"[^>]*>(.*?)</p>', visible, re.DOTALL)
    note = norm(nm.group(1)) if nm else ""

    hm = re.search(r'<div id="overlay-hint"[^>]*>(.*?)</div>', visible, re.DOTALL)
    hint = norm(hm.group(1)) if hm else ""

    return sections, note, hint


def find_val(rows: dict[str, str], keyword: str) -> tuple[str | None, str | None]:
    kw = keyword.lower()
    for label, val in rows.items():
        if kw in label.lower():
            return label, val
    return None, None


# --------------------------------------------------------------------------- #
# Core check
# --------------------------------------------------------------------------- #
def run_checks(repo: Path) -> tuple[list[str], dict[str, int]]:
    """Return (errors, summary). errors[0] may be a '__STRUCT__' sentinel to
    signal a structural parse failure (exit 2 vs 1)."""
    errors: list[str] = []
    summary = {"kb_rows": 0, "gp_rows": 0, "extras": 0}

    def read(rel: Path) -> str | None:
        p = repo / rel
        if not p.is_file():
            errors.append(f"__STRUCT__ source/surface missing: {rel}")
            return None
        return p.read_text(encoding="utf-8")

    index_src = read(INDEX_REL)
    bindings_src = read(BINDINGS_REL)
    sdl_src = read(SDL_REL)
    if index_src is None or bindings_src is None or sdl_src is None:
        return errors, summary

    kb = parse_kdefault(bindings_src)
    gp = parse_kgpdefault(bindings_src)
    if not kb:
        errors.append(f"__STRUCT__ could not parse kDefault[] in {BINDINGS_REL}")
    if not gp:
        errors.append(f"__STRUCT__ could not parse kGpDefault[] in {BINDINGS_REL}")

    sections, note, hint = extract_surfaces(index_src)
    for need in ("Movement", "Combat", "System", "Gamepad"):
        if need not in sections:
            errors.append(f"__STRUCT__ controls section '{need}' not found in {INDEX_REL}")
    if not note:
        errors.append(f"__STRUCT__ .overlay-note paragraph not found in {INDEX_REL}")
    if not hint:
        errors.append(f"__STRUCT__ #overlay-hint div not found in {INDEX_REL}")

    web_help = extract_web_help_text(sdl_src)
    if web_help is None:
        errors.append(
            f"__STRUCT__ could not isolate the __EMSCRIPTEN__ H-help "
            f'"=== CONTROLS ===" printf arm in {SDL_REL} (expected exactly one '
            f"#ifdef __EMSCRIPTEN__ arm containing it)"
        )

    if any(e.startswith("__STRUCT__") for e in errors):
        return errors, summary

    visible = strip_noise(index_src)

    # -- 1. F-KEY REGRESSION LOCK (WEB-001) -------------------------------- #
    fkey_re = re.compile(r"\bF([1-9]|1[0-2])\b")
    for m in fkey_re.finditer(visible):
        ctx = norm(visible[max(0, m.start() - 30): m.end() + 30])
        errors.append(
            f"[F-key lock/WEB-001] web/index.html player-facing surface names "
            f"'{m.group(0)}' (…{ctx}…). Dev keys F1/F2/F3-F12 are gated behind "
            f"GE007_DEV_HOTKEYS and must NEVER be advertised. Remove it; put any "
            f"rationale in an HTML comment (comments are exempt)."
        )
    # The __EMSCRIPTEN__ H-help console text is player-facing on web too — an
    # F-key re-advertised there is the same WEB-001 regression. Scanned over
    # string literals only (C comments exempt); the native #else arm is exempt.
    for m in fkey_re.finditer(web_help):
        ctx = norm(web_help[max(0, m.start() - 30): m.end() + 30])
        errors.append(
            f"[F-key lock/WEB-001] platform_sdl.c __EMSCRIPTEN__ H-help text "
            f"names '{m.group(0)}' (…{ctx}…). The web build has no overlay and "
            f"dev keys are gated behind GE007_DEV_HOTKEYS — remove it from the "
            f"__EMSCRIPTEN__ printf arm (the native #else arm is the exempt one)."
        )
    # Code must still dev-gate the F-keys, or the lock's premise is void.
    if not re.search(r"SDLK_F1\s*&&\s*pcDevHotkeysEnabled", sdl_src):
        errors.append(
            "[F-key lock] platform_sdl.c no longer gates F1 behind "
            "pcDevHotkeysEnabled() — the WEB-001 premise changed; re-evaluate "
            "whether the page F-key lock still applies (KEYDOWN handler)."
        )
    if not re.search(r"SDLK_F2\s*&&\s*pcDevHotkeysEnabled", sdl_src):
        errors.append(
            "[F-key lock] platform_sdl.c no longer gates F2 behind "
            "pcDevHotkeysEnabled() (KEYDOWN handler)."
        )
    if not re.search(
        r"SDLK_F3\b.{0,200}SDLK_F12\b.{0,200}pcDevHotkeysEnabled",
        sdl_src, re.DOTALL,
    ):
        errors.append(
            "[F-key lock] platform_sdl.c no longer gates the F3..F12 range "
            "behind pcDevHotkeysEnabled() (KEYDOWN handler)."
        )

    mv = sections["Movement"]
    cb = sections["Combat"]
    sysrows = sections["System"]
    gprows = sections["Gamepad"]

    def check_row(surface: str, rows: dict[str, str], keyword: str,
                  tokens: list[str], where: str, expect_desc: str) -> None:
        label, val = find_val(rows, keyword)
        if val is None:
            errors.append(
                f"[{surface}] no row matching '{keyword}'. Expected "
                f"{expect_desc}. Fix: {where}."
            )
            return
        for tok in tokens:
            if not has_token(val, tok):
                errors.append(
                    f"[{surface}] row '{label}' = '{val}' is missing '{tok}'. "
                    f"Engine ground truth: {expect_desc}. Fix: {where}."
                )

    # -- 2 & 3. Keyboard + high-value rows --------------------------------- #
    # Move (WASD) vs kDefault forward/back/left/right.
    kb_move = [kb.get(a, "?") for a in ("IB_FORWARD", "IB_BACK", "IB_LEFT", "IB_RIGHT")]
    if kb_move == ["W", "S", "A", "D"]:
        check_row("controls/Movement", mv, "Move", ["W", "A", "S", "D"],
                  "web/index.html Movement <dl>", "Move = WASD (kDefault)")
    else:
        errors.append(
            f"[input_bindings.c] kDefault movement keys changed to {kb_move} "
            f"(expected W/S/A/D) — the page's 'Move = WASD' row and the H help "
            f"WASD line need review."
        )
    summary["kb_rows"] += 1

    # Fire / Aim vs kDefault (distinguishes L vs R Shift).
    for act, kw, surface in (
        ("IB_FIRE", "Fire", "controls/Combat"),
        ("IB_AIM", "Aim", "controls/Combat"),
    ):
        scan = kb.get(act, "?")
        tok = KB_SCAN_TOKEN.get(scan)
        if tok is None:
            errors.append(
                f"[input_bindings.c] kDefault[{act}] = SDL_SCANCODE_{scan} has no "
                f"page-token mapping — update KB_SCAN_TOKEN and the page if this "
                f"default was intentionally rebound."
            )
        else:
            check_row(surface, cb, kw, [tok],
                      "web/index.html Combat <dl>", f"{kw} = {tok} (kDefault[{act}])")
        summary["kb_rows"] += 1

    # Reload (R) vs kDefault[IB_RELOAD].
    reload_scan = kb.get("IB_RELOAD", "?")
    reload_tok = KB_SCAN_TOKEN.get(reload_scan, reload_scan)
    check_row("controls/Combat", cb, "Reload", [reload_tok],
              "web/index.html Combat <dl>", f"Reload = {reload_tok} (kDefault[IB_RELOAD])")
    summary["kb_rows"] += 1

    # Use / interact (F) — anchored on the IB_RELOAD hardcoded alternate.
    if re.search(r"IB_RELOAD\b[^\n]*\(F\s*/", bindings_src):
        check_row("controls/Combat", cb, "Use", ["F"],
                  "web/index.html Combat <dl>",
                  "Use/interact = F (hardcoded IB_RELOAD alternate, input_bindings.c)")
    else:
        errors.append(
            "[input_bindings.c] the IB_RELOAD 'F / Backspace hardcoded alternate' "
            "comment is gone — the page's 'Use / interact = F' row is no longer "
            "anchored; confirm F still triggers interact (stubs.c) and update this "
            "gate + the page."
        )
    summary["kb_rows"] += 1

    # Lean (Q/E) vs kDefault.
    lean_l = kb.get("IB_LEAN_L", "?")
    lean_r = kb.get("IB_LEAN_R", "?")
    check_row("controls/Movement", mv, "Lean",
              [KB_SCAN_TOKEN.get(lean_l, lean_l), KB_SCAN_TOKEN.get(lean_r, lean_r)],
              "web/index.html Movement <dl>", f"Lean = {lean_l}/{lean_r} (kDefault)")
    summary["kb_rows"] += 1

    # Crouch (C) — page advertises C only (WEB-008); engine must still compare it.
    check_row("controls/Movement", mv, "Crouch", ["C"],
              "web/index.html Movement <dl>", "Crouch names C")
    if not sym_compared(sdl_src, "SDLK_c"):
        errors.append(
            "[platform_sdl.c] KEYDOWN handler no longer compares SDLK_c, but "
            "web/index.html still advertises Crouch = C. Fix the page or restore "
            "the binding."
        )
    summary["extras"] += 1

    # Watch / pause (Esc + Tab).
    check_row("controls/System", sysrows, "Watch", ["Esc", "Tab"],
              "web/index.html System <dl>", "Watch/pause = Esc / Tab")
    for sdlk, label in (("SDLK_ESCAPE", "Esc"), ("SDLK_TAB", "Tab")):
        if not sym_compared(sdl_src, sdlk):
            errors.append(
                f"[platform_sdl.c] KEYDOWN handler no longer compares {sdlk}, but "
                f"web/index.html still advertises Watch = {label}. Fix the page or "
                f"restore the binding."
            )
    summary["extras"] += 1

    # Mute (M).
    check_row("controls/System", sysrows, "Mute", ["M"],
              "web/index.html System <dl>", "Mute audio = M")
    if not sym_compared(sdl_src, "SDLK_m"):
        errors.append(
            "[platform_sdl.c] KEYDOWN handler no longer compares SDLK_m, but "
            "web/index.html still advertises Mute = M."
        )
    summary["extras"] += 1

    # Help (H).
    check_row("controls/System", sysrows, "Help", ["H"],
              "web/index.html System <dl>", "Help = H")
    if not sym_compared(sdl_src, "SDLK_h"):
        errors.append(
            "[platform_sdl.c] KEYDOWN handler no longer compares SDLK_h, but "
            "web/index.html still advertises Help = H."
        )
    summary["extras"] += 1

    # -- 4. Gamepad defaults vs kGpDefault --------------------------------- #
    gp_checks = [
        ("Fire", ["GB_FIRE", "GB_AIM"]),          # "Fire / Aim" -> RT / LT
        ("Jump", ["GB_JUMP"]),
        ("Reload", ["GB_RELOAD"]),
        ("Next", ["GB_WEAPON_NEXT"]),
        ("Prev", ["GB_WEAPON_PREV"]),
        ("Crouch", ["GB_CROUCH"]),
        ("Watch", ["GB_PAUSE"]),
    ]
    for keyword, actions in gp_checks:
        tokens = [GP_TOKEN.get(gp.get(a, "?"), gp.get(a, "?")) for a in actions]
        desc = " / ".join(f"{a}={t}" for a, t in zip(actions, tokens))
        check_row("controls/Gamepad", gprows, keyword, tokens,
                  "web/index.html Gamepad <dl>", f"kGpDefault {desc}")
        summary["gp_rows"] += 1

    # -- 5. overlay-note / overlay-hint ------------------------------------ #
    for tok in ("Esc", "Tab", "M", "H"):
        if not has_token(note, tok):
            errors.append(
                f"[overlay-note] '{note}' is missing '{tok}'. The .overlay-note "
                f"line must name Esc/Tab (watch), M (mute), H (help). "
                f"Fix: web/index.html .overlay-note."
            )
    for tok in ("Esc", "M", "H"):
        if not has_token(hint, tok):
            errors.append(
                f"[overlay-hint] '{hint}' is missing '{tok}'. The #overlay-hint "
                f"line must name Esc (release), M (mute), H (help). "
                f"Fix: web/index.html #overlay-hint."
            )

    return errors, summary


# --------------------------------------------------------------------------- #
# Self-test (negative control)
# --------------------------------------------------------------------------- #
SEEDS = [
    (
        "F1 row injected into controls table",
        INDEX_REL,
        ("<dt>Look</dt><dd>Mouse</dd>",
         "<dt>Look</dt><dd>Mouse</dd>\n<dt>Fly cam</dt><dd><kbd>F1</kbd></dd>"),
    ),
    (
        "page Reload rebound to T (kDefault still R)",
        INDEX_REL,
        ("<dt>Reload</dt><dd><kbd>R</kbd></dd>",
         "<dt>Reload</dt><dd><kbd>T</kbd></dd>"),
    ),
    (
        "kDefault IB_FIRE silently swapped to R Shift (page still L Shift)",
        BINDINGS_REL,
        ("SDL_SCANCODE_LSHIFT,  // IB_FIRE",
         "SDL_SCANCODE_RSHIFT,  // IB_FIRE"),
    ),
    (
        "crouch SDLK_c removed from KEYDOWN (page still C)",
        SDL_REL,
        ("event.key.keysym.sym == SDLK_c ||",
         "event.key.keysym.sym == SDLK_z ||"),
    ),
    (
        "overlay-hint drops M (mute)",
        INDEX_REL,
        ("<kbd>M</kbd> mute · <kbd>H</kbd> help\n</div>",
         "<kbd>H</kbd> help\n</div>"),
    ),
    (
        "gamepad Jump rebound to X (page still A)",
        BINDINGS_REL,
        ("SDL_CONTROLLER_BUTTON_A,                         /* GB_JUMP",
         "SDL_CONTROLLER_BUTTON_X,                         /* GB_JUMP"),
    ),
    (
        # Anchors on the "(FPS overlay is on by default…)" line, which exists
        # ONLY in the __EMSCRIPTEN__ arm — proves the lock covers the web
        # H-help while the native #else arm (which really names F1/F10, and
        # passes at HEAD) stays exempt.
        "F10 re-advertised in the web (__EMSCRIPTEN__) H-help text",
        SDL_REL,
        ('"(FPS overlay is on by default in the top-right.)\\n"',
         '"F10         FPS overlay toggle\\n"\n'
         '                        '
         '"(FPS overlay is on by default in the top-right.)\\n"'),
    ),
]


def self_test(repo: Path) -> int:
    src_files = [INDEX_REL, BINDINGS_REL, SDL_REL]
    print("=== self-test: pristine tree must PASS ===")
    errors, _ = run_checks(repo)
    if errors:
        print("  UNEXPECTED: pristine tree FAILED:", file=sys.stderr)
        for e in errors:
            print(f"    - {e}", file=sys.stderr)
        return 1
    print("  OK: pristine tree passes.\n")

    all_ok = True
    for name, rel, (old, new) in SEEDS:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for f in src_files:
                dst = root / f
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(repo / f, dst)
            target = root / rel
            text = target.read_text(encoding="utf-8")
            if old not in text:
                print(f"  SEED SETUP FAILED ({name}): anchor not found in {rel}",
                      file=sys.stderr)
                all_ok = False
                continue
            target.write_text(text.replace(old, new, 1), encoding="utf-8")
            errs, _ = run_checks(root)
            if errs:
                sample = next((e for e in errs if not e.startswith("__STRUCT__")), errs[0])
                print(f"  OK: seed '{name}' -> RED ({len(errs)} error(s)). e.g.: {sample}")
            else:
                print(f"  UNEXPECTED: seed '{name}' did NOT trip the gate.",
                      file=sys.stderr)
                all_ok = False

    print()
    if all_ok:
        print("=== self-test PASSED: pristine green, every seed red ===")
        return 0
    print("=== self-test FAILED ===", file=sys.stderr)
    return 1


# --------------------------------------------------------------------------- #
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo-root", type=Path, default=DEFAULT_REPO_ROOT)
    ap.add_argument("--self-test", action="store_true",
                    help="negative control: seed drifts into throwaway copies "
                         "and prove the gate flips red.")
    args = ap.parse_args()
    repo = args.repo_root.resolve()

    if args.self_test:
        return self_test(repo)

    errors, summary = run_checks(repo)
    if errors:
        struct = [e for e in errors if e.startswith("__STRUCT__")]
        drift = [e for e in errors if not e.startswith("__STRUCT__")]
        print("check_controls_doc: FAIL", file=sys.stderr)
        for e in struct:
            print(f"  - (structural) {e[len('__STRUCT__ '):]}", file=sys.stderr)
        for e in drift:
            print(f"  - {e}", file=sys.stderr)
        # Structural parse failure -> exit 2; a genuine documented drift -> 1.
        return 2 if struct and not drift else 1

    print("check_controls_doc: OK")
    print(f"  keyboard rows checked : {summary['kb_rows']}")
    print(f"  gamepad rows checked  : {summary['gp_rows']}")
    print(f"  hardcoded extras      : {summary['extras']} (crouch/watch/mute/help)")
    print("  F-key lock            : clean (no F1-F12 on any player-facing surface)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
