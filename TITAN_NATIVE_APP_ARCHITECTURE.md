
## CP15 asynchronous network service
Network I/O remains a Titan service, not an application-owned subsystem. Apps submit a bounded HTTP/HTTPS GET, poll status, and copy completed bytes through the ABI. Cancellation and deadline handling terminate at the shared Titan Network Client/HTTP transport. App shutdown abandons the session generation before the `.tmod` image is unloaded, preventing a late network worker from calling or writing into unloaded app code/data.
