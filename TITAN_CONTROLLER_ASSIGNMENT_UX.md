# Titan Controller Assignment UX — BuildFix29

Identity and player assignment are separate. Titan keeps stable device keys and preferred assignment hints, preserves Player slots across safe reconnects, and avoids reshuffling other players on ordinary hotplug.

When Welcome has no Player 1, the first meaningful eligible input wins deterministically. Connection/reconnect/assignment events are coalesced. Events occurring during boot/Health & Safety/Welcome are deferred; HOME emits one card after startup is ready. Later hotplug is shown immediately. Battery is included only when valid.
