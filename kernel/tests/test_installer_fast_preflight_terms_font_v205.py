#!/usr/bin/env python3
from pathlib import Path

kernel = Path(__file__).resolve().parents[1]
installer = (kernel / 'src/installer/titan_installer.c').read_text(encoding='utf-8')
font = (kernel / 'src/graphics/titan_font.c').read_text(encoding='utf-8')

def req(cond, msg):
    if not cond:
        raise SystemExit('FAIL: ' + msg)

req('#define TITAN_INSTALLER_VERSION "0.2.' in installer, 'installer version marker missing')

# Startup must not parse/CRC the TINS before the first interactive page.
startup = installer[installer.index('TitanInstallerModel_Detect(&s.console);'):installer.index('for (;;) {\n        s.frame++;')]
req('update_preflight(&s);' not in startup, 'startup still performs TINS preflight')
req('probe_mc(&s);' in startup, 'startup MC probe missing')

# Verification preflight is metadata-only. Full payload CRC remains in staging.
preflight = installer[installer.index('static int preflight_payload'):installer.index('static int install_entry_enabled')]
req('load_manifest(s)' in preflight, 'fast preflight does not validate TINS header/table')
req('crc_exact_stream' not in preflight, 'fast preflight still streams payload CRC')
req('discard_exact_stream' not in preflight, 'fast preflight still walks complete payload')
stage = installer[installer.index('static int install_job_step'):installer.index('static void draw_progress')]
req('job->source_crc = crc32_update' in stage, 'source CRC missing during transactional copy')
req('job->verify_crc = crc32_update' in stage and 'TITAN_INSTALL_JOB_VERIFY_READ' in stage,
    'staged-file incremental CRC verification missing')

# Terms layout must advance by actual wrapped line count, not fixed item rows.
req('draw_wrapped_block' in installer, 'variable-height terms wrapper missing')
req('terms_y = next_y' in installer, 'terms screen does not advance by rendered block height')
req('182.0f + (float)(i - (unsigned int)s->terms_scroll) * 28.0f' not in installer,
    'legacy fixed-row terms layout still present')

# Canonical Unicode remains in source; renderer handles the unsupported atlas symbols procedurally.
for literal in ('⚠', '™', '®', '•'):
    req(literal in installer, f'canonical terms character removed: {literal}')
for cp in ('0x00AEu', '0x2022u', '0x2122u', '0x2191u', '0x2193u', '0x26A0u'):
    req(cp in font, f'procedural glyph support missing: {cp}')
req('TitanFont_IsProceduralSymbol' in font and 'TitanFont_DrawProceduralSymbol' in font,
    'procedural Unicode renderer missing')
req('TitanFont_GetProceduralAdvance(codepoint, scale)' in font,
    'procedural symbol measurement/advance missing')

print('installer-fast-preflight-terms-font-v2.0.5: OK')
