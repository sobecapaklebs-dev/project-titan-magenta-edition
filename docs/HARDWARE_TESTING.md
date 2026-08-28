# Hardware Validation

Emulator success is useful, but Project Titan release gates require physical PS2 validation.

## Minimum hardware test pass

For every candidate release:

1. Cold boot with the target boot method.
2. Verify controller/input availability before and after HOME.
3. Verify USB remains active during library scanning.
4. Exercise HOME, top menu, Settings, Notifications and Library.
5. Trigger a full library rescan.
6. Install and launch at least one Titan Native application.
7. Launch an ISO/Neutrino title and a UL title when available.
8. Exercise background loading and fallback behavior.
9. Check language switching for EN, PT-BR and ES.
10. Generate diagnostics after any failure.

## Library scanner

The library scanner is a hardware-sensitive path because PS2 storage operations can block.

Important invariants:

- the frame/UI path must remain responsive;
- incomplete `.install-*` transaction directories must not be treated as installed apps;
- missing legacy roots must not cause an indefinite blocking probe;
- library progress should move through games, metadata, applications and finalization;
- diagnostic events should bracket storage operations closely enough to identify the exact blocking call.

## Artifact verification

After copying a kernel or package to USB, compare source and destination:

```bash
sha256sum source-file usb-file
cmp -s source-file usb-file
```

A test result is not valid when the USB copy differs from the build artifact.

## Reporting a freeze

Capture:

- photo/video of the last visible state;
- current/last Titan debug log or trace;
- exact kernel SHA-256;
- USB directory topology relevant to the failure;
- source revision / patch state;
- controller, USB and network hardware connected at the time.

Avoid relying on memory for the exact final screen percentage or message when a photo/log can preserve it.
