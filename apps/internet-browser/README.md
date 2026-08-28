# Internet — Titan Browser system app

Title ID: `TITN00010`

This application is a **kernel-resident Project Titan system app**. The TPKG
contains metadata and its app-local tile icon, but deliberately contains no ELF
or `.tmod`. The installed manifest uses the reserved entrypoint
`@system-browser`; selecting the Home tile routes directly to the browser page
inside the running Titan shell.

The browser runtime is implemented by:

- `kernel/src/services/titan_browser.c/.h`
- `kernel/src/services/titan_browser_net.c/.h`
- the Browser page in `kernel/src/ui/titan_shell_scene.c`

Current BuildFix16 scope:

- lightweight HTML text/link parsing;
- URL entry through Titan's existing text-input UI;
- back/forward history and scrolling;
- direct HTTP transport through PS2IP;
- HTTPS URL translation through the configurable Titan Stream Bridge;
- kernel-resident execution: no ExecPS2, external ELF, or Titan `.tmod` launch.

Downloads and the old external MPEG-2 stream-player handoff are intentionally
not exposed in this first internal build. They must be reintroduced only after
storage/network ownership and an in-system media player are hardware-proven.

The package-local icon is not a Project Titan curated system asset.
`kernel/assets` and `bootloader/assets` are unchanged by this port.

Build the package from the Project Titan root with:

```text
python3 kernel/tools/titan_pkg.py build \
  --source apps/internet-browser/package_source \
  --output Internet-TITN00010-1.1.1-system-browser.pkg \
  --title-id TITN00010 \
  --title Internet \
  --version 1.1.1 \
  --type system-browser \
  --author "Project Titan" \
  --description "Titan Browser integrado ao sistema. HTTP direto; HTTPS via Titan Stream Bridge."
```
