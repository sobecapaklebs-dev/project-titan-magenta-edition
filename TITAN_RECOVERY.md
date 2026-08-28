# Titan Recovery — BF30

Recovery is fail-safe and preserves the last known-working boot baseline. Invalid/missing/corrupt TINS content must stop installation/publication rather than execute or commit partial state. Installer rollback/backup behavior and boot diagnostics remain authoritative.

Required external recovery tests: cold boot with missing system payload, corrupt package, interrupted install/update, failed USB mount, recovery entry, and restoration of a last-known-good package. None is represented as hardware PASS by host contracts.
