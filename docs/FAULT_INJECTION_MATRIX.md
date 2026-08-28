# Fault-injection matrix

| Fault | Expected result | Host evidence |
|---|---|---|
| short read/write | exact-I/O retries or fails without publish | storage, audio, cache tests |
| profile power loss | recover valid `.bak`/`.tmp` generation | profile recovery variants |
| corrupt/truncated VMC | never reports READY | VMC corruption tests |
| malformed/overlapping ELF | rejected by shared validator | boot/application tests |
| partial library/UL record | scanner fails transaction | library tests |
| malformed WAV/chunk overflow | rejected within file/RAM budget | audio runtime tests |
| partial audio enqueue | unsent tail retained | audio runtime tests |
| fragmented/full FHI table | no overlapping commit | allocator contract test |
| controller reconnect held button | neutral first stable frame | input contract |
| video-mode failure | old mode restored or renderer fails closed | renderer contract; hardware pending |
| notification queue full | coalesce/priority replacement/drop counter | hardening contract |
| invalid UTF-8 | U+FFFD and one-byte progress | UTF-8 sanitizer test |

Physical PS2 tests remain mandatory for GS presentation, audio timing, IOP module
interactions, VMC use in games and per-title launch compatibility.
