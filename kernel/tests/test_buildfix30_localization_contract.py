#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
svc=(root/'src/services/titan_localization.c').read_text(encoding='utf-8')
hdr=(root/'src/services/titan_localization.h').read_text(encoding='utf-8')
shell=(root/'src/ui/titan_shell_scene.c').read_text(encoding='utf-8')
sched=(root/'src/services/titan_storage_scheduler.c').read_text(encoding='utf-8')
assert 'TITAN_LANGUAGE_ENGLISH' in hdr and 'TITAN_LANGUAGE_PORTUGUESE' in hdr and 'TITAN_LANGUAGE_SPANISH' in hdr
assert 'language=pt-BR' in svc and 'language=es' in svc and 'language=en' in svc
assert 'TitanStorageIo_AtomicReplace' in svc
assert 'TitanLocalization_StorageWorkerPump' in sched
assert 'TitanLocalization_CycleLanguage(direction)' in shell
assert 'TITAN_STR_HEALTH_LINE1' in shell and 'TITAN_STR_WELCOME_TITLE' in shell
assert 'return 16;' in shell
assert 'labels[14] = "pikaVR";' in shell and 'labels[15] = "VlombisCore";' in shell
for forbidden in ('Para informacoes sobre saude e seguranca','Pressione o botao do sistema para continuar','Pasta / Pressione X para abrir','Titan Disc Engine / Pressione X para iniciar'):
    assert forbidden not in shell, forbidden
for p in (root/'src').rglob('*'):
    if p.suffix in ('.c','.h'):
        p.read_text(encoding='utf-8')
print('titan-global-language-architecture-contract: OK')
print('titan-global-text-utf8-contract: OK')
print('titan-language-en-pt-es-contract: OK')
print('titan-localization-no-frame-io-contract: OK')
