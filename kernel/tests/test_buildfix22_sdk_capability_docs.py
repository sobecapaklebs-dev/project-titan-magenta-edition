#!/usr/bin/env python3
import pathlib,sys
root=pathlib.Path(sys.argv[1]).parent
status=(root/'docs/PIKAVR_VLOMBISCORE.md').read_text()
game=(root/'docs/TITAN_GAME_SDK.md').read_text()
app=(root/'docs/TITAN_APP_SDK.md').read_text()
for token in ['vram_saved','POINT/LINEAR','PSM8+CLUT','HOST VALIDATED','no public pikaVR ABI']:
    assert token.lower() in status.lower(), token
assert 'capability declaration with\nzero measured events is insufficient'.replace('\n',' ') in status.replace('\n',' ')
assert 'do not freeze a public pikavr abi' in game.replace('\n',' ').lower()
for token in ['app.cfg','title.cfg','external ELF handoff','SAS compatibility','SAS compliance','INSTALL.PBT']:
    assert token in app, token
assert 'Canonical classic SAS metadata is read from `title.cfg`' in app
assert '`app.cfg` remains a' in app and 'fallback' in app
print('buildfix22-sdk-capability-docs: OK')
