#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
scene=(root/'src/ui/titan_shell_scene.c').read_text()
catalog=(root/'src/ui/titan_shell_catalog.c').read_text()
icons=(root/'src/ui/titan_icons.c').read_text()
installer=(root/'src/services/titan_package_installer.c').read_text()
installer_h=(root/'src/services/titan_package_installer.h').read_text()
apps=(root/'src/services/titan_applications.c').read_text()
update=(root/'src/services/titan_system_update.c').read_text()
make=(root/'Makefile').read_text()
assert 'TITAN_ICON_LIBRARY' in catalog and 'TITAN_ICON_APPLICATIONS' in scene
assert catalog.index('\"Library\"') > catalog.index('\"Overview\"')
assert '\"Controller\"' not in catalog and '\"Storage\"' not in catalog
assert 'TITAN_SETTINGS_ACCESSORIES' in scene and 'TITAN_SETTINGS_STORAGE' in scene
assert '#define TITAN_SHELL_BASE_PREFIX_COUNT 1u' in scene
assert '#define TITAN_SHELL_STATIC_SUFFIX_COUNT 3u' in scene
assert 'Preserve the Applications glyph that used to live on Home' in scene
assert 'Atualizacao do software do sistema' in scene or 'System Software Update' in scene
assert 'TITAN_ICON_DEBUG_SETTINGS' in scene
assert 'src/services/titan_system_update.o' in make
assert 'TITAN_PACKAGE_CAPACITY 96u' in installer_h
assert 'TitanPackageInstaller_RefreshFromRoot(TITAN_PACKAGE_PRIMARY_ROOT);' in installer
assert 'TitanPackageInstaller_RefreshFromRoot(TITAN_PACKAGE_FALLBACK_ROOT);' in installer
assert 'if (g_package_installer.package_count == 0u)' not in installer[installer.index('unsigned int TitanPackageInstaller_Refresh(void)'):installer.index('unsigned int TitanPackageInstaller_GetCount', installer.index('unsigned int TitanPackageInstaller_Refresh(void)'))]
assert 'TitanApplications_AddInstalledDirectory' in installer
assert 'catalogo atualizado sem rescan completo' in scene or 'catalog updated without a full rescan' in scene
assert 'TitanStorageScheduler_SetPackageExclusiveMode(0);' in scene
assert 'TITAN_SYSTEM_UPDATE_CURRENT_BUILD ' in (root/'src/services/titan_system_update.h').read_text()
assert 'Downgrade/mesma versao bloqueado' in update
with tempfile.TemporaryDirectory() as d:
    d=Path(d); kernel=d/'kernel.elf'; kernel.write_bytes(b'BF23')
    pup=d/'next.pup'
    subprocess.run(['python3',str(root/'tools/titan_pup.py'),'build','--output',str(pup),'--build','25','--version','0.3.90-buildfix25','--file',f'kernel.elf={kernel}'],check=True,stdout=subprocess.DEVNULL)
    subprocess.run(['python3',str(root/'tools/titan_pup.py'),'verify',str(pup)],check=True,stdout=subprocess.DEVNULL)
print('buildfix20-update-package-contract: OK')
