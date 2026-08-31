# BuildFix27 Checkpoint CP24
STATUS: HOST_VALIDATED
PREVIOUS_VALID: CP23
PARENT_ARCHIVE: BUILDFIX27_CP23_INCREMENTAL.tar.gz
BLOCK: FTP passive LIST/NLST data plane

- Added EPSV and IPv4 PASV listeners over Titan Network Stream.
- Passive ports are bounded to 50000..50031 and remain Titan-owned opaque handles.
- LIST/NLST are cooperative: one VFS entry is paged and transmitted per Tick, never a whole directory buffered in app memory.
- Directory enumeration uses TitanStorageIo_ListDirectory only.
- Data listener/stream cleanup is deterministic on completion/failure/stop.
- RETR/STOR remain explicitly unavailable until transactional file-transfer state lands.
- Existing FTP control, Network Stream and wLaunchELF host contracts remain green.

NEXT: CP25 cooperative RETR + transactional STOR using temporary publication.
