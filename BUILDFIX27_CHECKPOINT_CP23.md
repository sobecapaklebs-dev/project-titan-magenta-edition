# BuildFix27 Checkpoint CP23
STATUS: HOST_VALIDATED
PREVIOUS_VALID: CP22
PARENT_ARCHIVE: BUILDFIX27_CP22_INCREMENTAL.tar.gz
BLOCK: Titan-owned FTP control service

- Added cooperative FTP control service over Titan Network Stream.
- USER/PASS anonymous login, PWD, CWD, TYPE, SYST, NOOP and QUIT are implemented.
- FTP service owns no sockets/FileXio; all control I/O uses opaque Network Stream handles and path checks use Titan Storage/VFS.
- Root is restricted to writable mass/massN/mc0/mc1 namespaces; `..` components are rejected.
- Data commands PASV/EPSV/LIST/NLST/RETR/STOR fail explicitly with 502 until CP24 data plane lands.
- Strict -Wall/-Wextra/-Werror host control fixture passes; Network Stream, wLaunchELF and Project NES regressions pass.

NEXT: CP24 passive data plane + LIST/RETR/STOR transactional storage semantics.
