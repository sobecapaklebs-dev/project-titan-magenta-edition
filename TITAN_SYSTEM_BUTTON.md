# Titan System Button — BuildFix29

DS3 PS, DS4 PS, DualSense PS and Xbox Guide/Home map to `TITAN_NBTN_SYSTEM`. The event belongs to Titan, not the application, while a Titan-managed session is active.

Native/Project NES routes the normalized edge through Session/Smart IGR/Overlay. UL and Neutrino receive an out-of-band PADEMU SIF system command so the button is not injected into game input. First press opens a supported overlay; pressing System while the overlay is active resumes according to the overlay policy. Reentrancy is blocked and the BF28 chord remains a compatibility fallback.
