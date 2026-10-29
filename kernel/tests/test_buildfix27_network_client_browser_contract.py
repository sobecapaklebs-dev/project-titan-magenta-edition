#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
kernel = root / "kernel"

def text(rel): return (root / rel).read_text(encoding="utf-8", errors="replace")
def req(ok, msg):
    if not ok: raise SystemExit("buildfix27-network-client-browser: FAIL: " + msg)

make = text("kernel/Makefile")
client_h = text("kernel/src/services/titan_network_client.h")
client_c = text("kernel/src/services/titan_network_client.c")
http_h = text("kernel/src/services/titan_http_client.h")
http_c = text("kernel/src/services/titan_http_client.c")
browser = text("kernel/src/services/titan_browser_net.c")
asset = text("kernel/src/services/titan_asset_service.c")

req("src/services/titan_network_client.o" in make, "Network Client is not linked into kernel")
req("TitanNetworkClient_GetEx" in client_h and "TitanNetworkClientOwner" in client_h,
    "reusable owner-aware Network Client API missing")
req("TITAN_NETWORK_CLIENT_OWNER_BROWSER" in client_h and "TITAN_NETWORK_CLIENT_OWNER_ASSET_SERVICE" in client_h,
    "Browser/Asset ownership identities missing")
req("TitanHttpClient_GetEx" in client_c, "Network Client does not delegate to bounded HTTP/TLS transport")
req(client_c.count("int state = DIntr();") >= 2 and client_c.count("if (state != 0) EIntr();") >= 2
    and "CpuSuspendIntr" not in client_c and "CpuResumeIntr" not in client_c,
    "single-flight ownership acquisition is not protected with current EE interrupt API")
req("requests_rejected_busy" in client_h and "requests_timed_out" in client_h,
    "ownership/timeout telemetry missing")

for forbidden in ("gethostbyname", "socket(", "connect(", "send(", "recv(", "lwip_close", "<ps2ip.h>", "<netdb.h>"):
    req(forbidden not in browser, f"Browser still owns direct network primitive: {forbidden}")
req("TitanNetworkClient_GetEx" in browser and "TITAN_NETWORK_CLIENT_OWNER_BROWSER" in browser,
    "Browser is not consuming Titan Network Client")
req("deadline_milliseconds = TITAN_BROWSER_NET_DEADLINE_MS" in browser,
    "Browser request deadline is not explicit")
req("should_cancel = TitanBrowserNet_ShouldCancel" in browser,
    "Browser cancellation is not routed into Network Client")
req("TitanStorageIo_AtomicReplace" in browser,
    "download publication no longer uses Titan Storage atomic commit")
req("TitanNetworkClient_GetEx" in asset and "TITAN_NETWORK_CLIENT_OWNER_ASSET_SERVICE" in asset,
    "Asset Service bypasses shared Network Client ownership")

req("select(transport->fd + 1" in http_c and "deadline_milliseconds" in http_h,
    "lower HTTP transport lost bounded wait/deadline capability")
req("br_x509_minimal_set_time" in http_c and "br_ssl_client_reset" in http_c,
    "TLS certificate/hostname validation path lost")

print("buildfix27-network-client-browser: OK shared ownership, bounded cancel/deadline, no Browser PS2IP")
