# Network + Theme Integration RT1 BuildFix1 — PS2SDK TCP/IP public header

Hardware/build feedback exposed an EE cross-build failure in `src/services/titan_network.c`:

```
fatal error: lwip/netif.h: No such file or directory
```

The RT1 service incorrectly included lwIP's internal/source-tree path. PS2SDK exports the EE TCP/IP structures (`struct netif`, `struct ip4_addr`, `IP4_ADDR`, etc.) through the public `tcpip.h` in `$(PS2SDK)/common/include`, while `ps2ip.h` exports the PS2IP/netif entry points.

BuildFix1 changes only:

```c
-#include <lwip/netif.h>
+#include <tcpip.h>
```

No functional delta to AR9271, NetMan, SMAP, DHCP, themes, Settings UI, installer transaction logic, or the frozen official bootloader BF6.
