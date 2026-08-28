# Licensing and Third-Party Components

Project Titan combines original code with third-party homebrew/open-source components. The public repository must preserve upstream license and attribution requirements.

## Original Project Titan code

Original Project Titan code is released under the **MIT License**, except where a file or component carries a different license notice. The repository-level `LICENSE` applies only to Project Titan material for which the Project Titan contributors have the right to grant that license.

Third-party and combined works keep their own license obligations. In particular, the vendored FCEUmm core is GPL-2.0 and a distributed Project NES module linked with FCEUmm is treated as a GPL-2.0 combined work with the required upstream license/provenance material. The MIT license does not relicense FCEUmm, Free McBoot, or any other third-party component.

## Third-party source

For every vendored component:

1. preserve the upstream copyright notice;
2. preserve its license file;
3. record the upstream project/revision when practical;
4. do not relicense third-party files as Project Titan code;
5. check whether binary redistribution has additional requirements.

Examples of third-party areas that may require their own notices include emulator cores, launchers, PS2SDK/gsKit-related components, boot/installer dependencies and networking code.

## Commercial game content

Do not distribute commercial game ROMs, ISOs, BIOS images or packages containing them.

A private port that consumes a user-owned game dump should keep that dump outside the public repository and outside public GitHub Releases.

## Firmware

Do not redistribute firmware blobs unless the applicable license explicitly permits redistribution. Prefer documenting where the user should obtain the firmware legally.

## Artwork and trademarks

Only publish artwork you created, commissioned, generated with redistribution rights, or otherwise have permission to distribute. Product/platform names and trademarks belong to their respective owners.

## Disclaimer

This file is a project hygiene guide, not legal advice. For a public release with substantial third-party code, perform a final license audit of the exact files being published.

## Non-redistributed private audio

The private hardware-validation candidate may contain
`runtime/TITAN/audio/magenta_melody.wav`. That file was prepared from
user-provided audio and is intentionally omitted from the public source export
unless independent redistribution rights are established.

This omission does not remove the audio runtime implementation. Project Titan
continues to support a compatible user-supplied PCM WAV at the documented
runtime path.

## Deterministic test fixtures

The `kernel/tests/fixtures/hello_titan_*.tmod` files are tiny deterministic
unit-test fixtures used to exercise Titan module parsing and R5900/COP1 guards.
They are identity-locked by the pre-publication audit and are not application
release packages.
