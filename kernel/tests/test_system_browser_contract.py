#!/usr/bin/env python3
"""Lock the kernel-resident Internet system-app contract."""
from pathlib import Path
import sys

kernel = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
root = kernel.parent

makefile = (kernel / 'Makefile').read_text()
kernel_c = (kernel / 'src/core/titan_kernel.c').read_text()
browser = (kernel / 'src/services/titan_browser.c').read_text()
browser_net = (kernel / 'src/services/titan_browser_net.c').read_text()
apps_c = (kernel / 'src/services/titan_applications.c').read_text()
apps_h = (kernel / 'src/services/titan_applications.h').read_text()
installer = (kernel / 'src/services/titan_package_installer.c').read_text()
installer_h = (kernel / 'src/services/titan_package_installer.h').read_text()
shell = (kernel / 'src/ui/titan_shell_scene.c').read_text()
tool = (kernel / 'tools/titan_pkg.py').read_text()

assert 'src/services/titan_browser.o' in makefile
assert 'src/services/titan_browser_net.o' in makefile
assert '-lps2ip' in makefile
assert 'TitanBrowser_Initialize();' in kernel_c
assert 'TitanBrowser_Update();' in kernel_c

assert 'TITAN_APPLICATION_SYSTEM_BROWSER' in apps_h
assert 'TITAN_PACKAGE_APP_SYSTEM_BROWSER' in installer_h
assert 'system-browser' in apps_c
assert '@system-browser' in apps_c
assert 'system-browser' in installer
assert '@system-browser' in installer
assert 'APP_SYSTEM_BROWSER = 2' in tool
assert 'SYSTEM_BROWSER_ENTRYPOINT = "@system-browser"' in tool

# Launching Internet must enter the in-shell browser page, never ExecPS2/tmod.
assert 'TitanApplications_IsSystemBrowser(application_index)' in shell
assert 'g_scene.page = TITAN_SHELL_PAGE_BROWSER;' in shell
assert 'TitanBrowser_GoHome();' in shell
assert 'TitanExecHandoff' not in browser
assert 'LoadExecPS2' not in browser

# The first internal build must not hand video to an external player or expose
# download writes through the UI while storage/network ownership is being proven.
assert 'The old POC handed' in browser
assert 'Downloads ficam desativados nesta primeira versao interna' in shell
assert 'Video ainda precisa ser portado para o player interno do Titan' in shell

# Networking worker is lazy: merely installing/booting the system app cannot
# create an extra EE thread until the first HTTP request is submitted.
init_start = browser_net.index('void TitanBrowserNet_Initialize(void)')
init_end = browser_net.index('\n}', init_start) + 2
init_body = browser_net[init_start:init_end]
assert 'CreateThread' not in init_body
assert 'StartThread' not in init_body
assert 'TitanBrowserNet_EnsureWorker' in browser_net

# Browser package assets are app-local; system-curated assets remain untouched.
assert (root / 'apps/internet-browser/package_source/icon.rgb565').stat().st_size == 64 * 64 * 2

print('system-browser-contract: OK')
