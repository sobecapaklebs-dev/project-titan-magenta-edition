#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
mk=(root/'Makefile').read_text(); tls=(root/'tools/titan_tls_deps.py').read_text(); notice=(root/'THIRD_PARTY_TLS_NOTICE.md').read_text(); lic=(root/'third_party/bearssl_ps2/BEARSSL_LICENSE.txt').read_text()
checks={'native-http-object':'src/services/titan_http_client.o' in mk,'native-image-object':'src/services/titan_asset_image.o' in mk,'bearssl-lib':'-lbearssl' in mk and 'libbearssl.a' in mk,'png-z-jpeg-libs':all(x in mk for x in ('-lpng','-lz','-ljpeg')),'ports-path':'$(PS2SDK)/ports/include' in mk and '$(PS2SDK)/ports/lib' in mk,'tls-ca-generated':'titan_tls_ca_bundle.inc' in mk and 'titan_tls_deps.py' in mk,'tls-pinned-ca':'ATHENA_CA_SHA256' in tls and 'c91316e5e83e480b5275be02fdfb810ae834c47704b6eb8d1d36a9da4569779d' in tls,'bearssl-06':'bearssl-0.6.tar.gz' in tls and 'bearssl-0.6' in mk,'license-preserved':'Copyright (c) 2016 Thomas Pornin' in lic and 'Permission is hereby granted' in lic,'notice-license-link':'BEARSSL_LICENSE.txt' in notice}
for n,v in checks.items():
 if not v: raise SystemExit('asset-build-contract: FAIL '+n)
 print('asset-build-contract: OK '+n)
print('asset-build-contract: PASS')
