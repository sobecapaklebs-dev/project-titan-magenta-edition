# Titan Internet Time — BuildFix30

Titan uses bounded SNTP over UDP/123. Automatic Time never blocks boot or the shell: DNS, socket send/receive and response waiting execute only on a fixed-stack low-priority worker.

The client validates response length, leap-indicator state, server/broadcast mode, stratum, echoed request nonce and the server transmit timestamp before accepting a new UTC anchor. A request timeout is 2.5 seconds once the socket request is active. Automatic retry is delayed after failure and successful synchronization is refreshed on a multi-hour cadence rather than polling aggressively.

State is visible as Idle, Waiting for network, Pending, Synchronizing, Synchronized, DNS error, Network error, Timed out or Invalid response. The RTC/local clock remains usable when Internet Time fails.

No HTTP client, Browser stack or second network core is created for time synchronization; the target path uses the already-resident ps2ip network environment.
