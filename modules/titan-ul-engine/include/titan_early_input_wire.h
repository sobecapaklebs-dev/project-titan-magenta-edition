#ifndef TITAN_EARLY_INPUT_WIRE_H
#define TITAN_EARLY_INPUT_WIRE_H
#include <stdint.h>
#define TITAN_EARLY_INPUT_CONFIG_MAGIC 0x54454932u /* TEI2 */
#define TITAN_EARLY_INPUT_CONFIG_VERSION 1u
#define TITAN_EARLY_INPUT_BACKEND_NONE 0u
#define TITAN_EARLY_INPUT_BACKEND_USB  1u
#define TITAN_EARLY_INPUT_BACKEND_BT   2u
#define TITAN_EARLY_INPUT_BOND_VALID 0x54424f4eu /* TBON */
typedef struct TitanEarlyInputBondWire {
    uint32_t magic;
    uint8_t addr_type;
    uint8_t addr[6];
    uint8_t ltk[16];
    uint8_t irk[16];
    uint8_t identity_addr_type;
    uint8_t identity_addr[6];
} TitanEarlyInputBondWire;
typedef struct TitanEarlyInputConfigWire {
    uint32_t magic;
    uint16_t version;
    uint8_t backend;
    uint8_t flags;
    uint8_t pad_enable;
    uint8_t pad_vibration;
    uint16_t reserved;
    TitanEarlyInputBondWire xbox_bond;
} TitanEarlyInputConfigWire;
/* Reserved PadEmuSettings bits used only by Titan's system-shell loader.
 * Bits 16..23 historically use only the low three bits, leaving bit 23 free.
 * Byte 3 is already the historical "VMC / suppress SIO2 hook" byte. */
#define TITAN_PADEMU_SETTINGS_TITAN_TRANSPORT_ONLY 0x00800000u
#define TITAN_PADEMU_BOOT_EXT_MAGIC 0x54494246u /* TIBF */
typedef struct TitanPademuBootExtension {
    uint32_t magic;
    TitanEarlyInputBondWire xbox_bond;
} TitanPademuBootExtension;
#endif
