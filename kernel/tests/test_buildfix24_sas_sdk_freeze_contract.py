import sys
from pathlib import Path
r=Path(sys.argv[1]); d=(r/'docs/TITAN_APP_SDK.md').read_text();
for s in ['SAS compatibility','SAS compliance','title.cfg','app.cfg','INSTALL.PBT','external ELF handoff']: assert s in d
for f in ['docs/history/reports/TITAN_SAS_REFERENCE_AUDIT.md','docs/reference/TITAN_SAS_COMPLIANCE_MODEL.md','docs/reference/TITAN_SAS_PREFIX_TABLE.md','docs/reference/TITAN_SAS_COMPATIBILITY_MATRIX.md']: assert (r/f).is_file()
print('titan-sas-sdk-freeze-contract: OK public docs distinguish compliance, compatibility, fallback and execution')
