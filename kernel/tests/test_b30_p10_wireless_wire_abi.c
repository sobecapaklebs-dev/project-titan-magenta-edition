#include <stddef.h>
#include "titan_pademu_telemetry.h"

_Static_assert(sizeof(TitanPademuWirelessRequest) == 16u, "wireless request ABI drift");
_Static_assert(sizeof(TitanPademuWirelessAccessory) == 48u, "wireless accessory ABI drift");
_Static_assert(sizeof(TitanPademuWirelessState) == 224u, "wireless state ABI drift");
_Static_assert(sizeof(TitanPademuWirelessState) <= 256u, "wireless state exceeds EE SIF RPC buffer");
_Static_assert(offsetof(TitanPademuWirelessState, accessories) == 32u, "wireless state prefix drift");

int main(void)
{
    TitanPademuWirelessRequest req = { TITAN_PADEMU_WIRELESS_VERSION, TITAN_PADEMU_WIRELESS_QUERY, 0u, 0u };
    return (req.version == TITAN_PADEMU_WIRELESS_VERSION && req.command == TITAN_PADEMU_WIRELESS_QUERY) ? 0 : 1;
}
