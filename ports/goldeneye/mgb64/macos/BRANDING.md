# MGB64 — Brand Guide

## Name

**MGB64** is a neutral, engineering-focused codename framed around the build
process. "64" signals N64 heritage and the SM64 decompilation precedent without
using the original game's title as the project name.

Bundle identifier: `com.mgb64.app`

The brand name is centralized in `macos/Sources/BrandConfig.swift`. To rename
the app, change that file and Info.plist. Internal code uses "ge007".

## Design Language

### Colors

| Role | Hex | Name | Usage |
|------|-----|------|-------|
| Primary | `#4A6FA5` | Steel Blue | Buttons, links, active states, icon primary |
| Accent | `#D4A843` | Amber Gold | Highlights, progress indicators, icon accent |
| Background | `#1C1C1E` | Charcoal | Dark mode panels, splash screen |
| Surface | `#2C2C2E` | Graphite | Cards, grouped backgrounds |
| On Surface | `#FFFFFF` | White | Primary text on dark backgrounds |
| Subtle | `#8E8E93` | System Gray | Secondary text, disabled states |

Light mode: follow system defaults. These brand colors appear in accent
positions only — the app should feel native, not themed.

### App Icon

**Concept: Antenna Dish (top-down view)**

A minimalist satellite dish rendered as concentric circles (Steel Blue)
with a diagonal feed arm crossing the center (Amber Gold). The circular
form fills the macOS rounded-rect icon shape naturally.

```
     ╭─────────────╮
    │  ╭─────────╮  │
    │ │  ╭─────╮  │ │
    │ │ │  ╭─╮  │ │ │
    │ │ │ │ ● │ │ │ │    ← feed point (amber dot)
    │ │ │  ╰─╯  │ │ │
    │ │  ╰──/──╯  │ │    ← feed arm (amber diagonal)
    │  ╰───/───╯  │ │
    │   ╰─/─────╯  │
     ╰─────────────╯
```

The dish evokes radar, signals, surveillance — thematically appropriate
without depicting any copyrighted imagery.

The icon is generated during `build_app_bundle.sh` from
`Scripts/generate_app_icon.py`. The repository intentionally does not track PNG
or ICNS outputs, because binary media formats are blocked by the public
contamination guard.

### Typography

System font (SF Pro) exclusively. No custom fonts. Sizes follow Apple HIG:
- Window title: System default
- Headings: .title2 or .title3
- Body: .body
- Captions: .caption

### Iconography

SF Symbols throughout:
- ROM file: `doc.badge.gearshape`
- Settings: `gearshape`
- Gamepad: `gamecontroller`
- Video: `display`
- Audio: `speaker.wave.2`
- Fullscreen: `arrow.up.left.and.arrow.down.right`

### Voice & Tone

- Technical but approachable
- "An N64 game engine ported natively to macOS, built on a faithful decompilation"
- Be honest about status: a work-in-progress, community-iteration port — not a
  1:1 replacement for original hardware. Point to `PORT.md` for known limitations.
- Never: original-title-forward marketing, film-title phrasing, publisher-title
  phrasing, or claims that imply affiliation with any rights holder.
- Emphasis on the engineering achievement, not the game content

### About Panel

```
MGB64

Version 0.1.0
An N64 game engine ported natively to macOS, built on a faithful decompilation.
Bring your own ROM.

Work in progress — a community-iteration port, not a 1:1 replacement for
original hardware. See PORT.md for known limitations.

Not affiliated with Nintendo, Rare, Microsoft,
MGM, Danjaq LLC, or EON Productions.
```

Strings are centralized in `Sources/BrandConfig.swift` (`tagline`, `statusNote`,
`disclaimer`); the About panel renders them via `AboutView.swift`.
