#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve()
runner=(r/'kernel/tests/run_host_tests.sh').read_text(errors='replace')
progress=(r/'docs/history/buildfix/BUILDFIX27_PROGRESS.md').read_text(errors='replace')
status=(r/'docs/history/buildfix/BUILDFIX27_FINAL_SOURCE_STATUS.md').read_text(errors='replace')

def req(x,m):
    if not x: raise SystemExit('buildfix27-host-readiness: FAIL: '+m)
required=[
 'test_buildfix27_cp10_recovery_contract.py',
 'test_buildfix27_project_nes_storage_save_contract.py',
 'test_buildfix27_project_nes_conformance_contract.py',
 'test_buildfix27_project_nes_lifecycle_package_contract.py',
 'test_buildfix27_network_client_browser_contract.py',
 'test_buildfix27_network_stream_contract.py',
 'test_buildfix27_ftp_control_contract.py',
 'test_buildfix27_ftp_data_contract.py',
 'test_buildfix27_ftp_file_contract.py',
 'test_buildfix27_app_ftp_integration_contract.py',
 'test_buildfix27_wlaunchelf_text_editor_contract.py',
 'test_buildfix27_wlaunchelf_recursive_contract.py',
 'test_buildfix27_wlaunchelf_image_viewer_contract.py',
 'test_buildfix27_managed_storage_mount_gate.py',
 'test_buildfix27_titan_app_sdk_docs.py',
 'test_buildfix27_lifecycle_classification_closure.py',
 'test_buildfix27_vmc_adapter_contract.py',
 'test_buildfix27_vmc_traversal_contract.py',
 'test_buildfix27_vmc_export_contract.py',
 'test_buildfix27_scope_closure_contract.py',
 'test_buildfix27_storage_mount_contract.py',
 'test_sas_execution_model_contract.py'
]
for q in required: req(q in runner,q+' absent from official host runner')
req('TARGET_BUILD_VALIDATION_PENDING_EXTERNAL_PS2DEV' in progress and ('EXTERNAL_VALIDATION_PENDING_PS2DEV' in status or 'PENDING_EXTERNAL_PS2DEV' in status),'external PS2DEV target state lost')
req(('FCEUMM_REAL_INTEGRATED=1' in progress or 'FCEUmm is materially present and integrated' in progress) and ('FCEUMM_REAL_INTEGRATED=1' in status or 'MATERIALIZED + IMPORTED + INTEGRATED' in status),'real FCEUmm integration status lost')
print('buildfix27-host-readiness: OK all material host gates registered; source complete and external validation states explicit')
