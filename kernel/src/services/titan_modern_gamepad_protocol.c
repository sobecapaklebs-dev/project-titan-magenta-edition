#include "titan_modern_gamepad_protocol.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static int16_t les16(const uint8_t *p) { return (int16_t)le16(p); }
static uint8_t axis16_to_u8(int16_t value)
{
    int32_t v = (int32_t)value + 32768;
    if (v < 0) v = 0;
    if (v > 65535) v = 65535;
    return (uint8_t)((v * 255 + 32767) / 65535);
}
static uint8_t trig10_to_u8(uint16_t value)
{
    if (value > 1023u) value = 1023u;
    return (uint8_t)((value * 255u + 511u) / 1023u);
}
static void clear_result(TitanGamepadProtocolResult *out)
{
    memset(out, 0, sizeof(*out));
    out->state.left_x = out->state.left_y = 128u;
    out->state.right_x = out->state.right_y = 128u;
}
static void map_hat(uint8_t hat, uint32_t *buttons)
{
    static const uint32_t map[9] = {
        TITAN_NBTN_UP,
        TITAN_NBTN_UP|TITAN_NBTN_RIGHT,
        TITAN_NBTN_RIGHT,
        TITAN_NBTN_RIGHT|TITAN_NBTN_DOWN,
        TITAN_NBTN_DOWN,
        TITAN_NBTN_DOWN|TITAN_NBTN_LEFT,
        TITAN_NBTN_LEFT,
        TITAN_NBTN_LEFT|TITAN_NBTN_UP,
        0u
    };
    if (hat > 8u) hat = 8u;
    *buttons |= map[hat];
}
static int parse_dualsense_common(const uint8_t *p, size_t size, TitanGamepadProtocolResult *out)
{
    uint8_t b0,b1,b2,status0;
    uint32_t b=0u;
    if (!p || !out || size < 63u) return 0;
    clear_result(out);
    out->state.left_x=p[0]; out->state.left_y=p[1];
    out->state.right_x=p[2]; out->state.right_y=p[3];
    out->state.trigger_l=p[4]; out->state.trigger_r=p[5];
    b0=p[7]; b1=p[8]; b2=p[9];
    map_hat((uint8_t)(b0 & 0x0fu), &b);
    if (b0&0x10u) b|=TITAN_NBTN_SQUARE;
    if (b0&0x20u) b|=TITAN_NBTN_CROSS;
    if (b0&0x40u) b|=TITAN_NBTN_CIRCLE;
    if (b0&0x80u) b|=TITAN_NBTN_TRIANGLE;
    if (b1&0x01u) b|=TITAN_NBTN_L1;
    if (b1&0x02u) b|=TITAN_NBTN_R1;
    if (b1&0x04u) b|=TITAN_NBTN_L2;
    if (b1&0x08u) b|=TITAN_NBTN_R2;
    if (b1&0x10u) b|=TITAN_NBTN_SELECT;
    if (b1&0x20u) b|=TITAN_NBTN_START;
    if (b1&0x40u) b|=TITAN_NBTN_L3;
    if (b1&0x80u) b|=TITAN_NBTN_R3;
    if (b2&0x01u) b|=TITAN_NBTN_SYSTEM;
    if (b2&0x02u) b|=TITAN_NBTN_TOUCHPAD;
    if (b2&0x04u) b|=TITAN_NBTN_MIC;
    out->state.buttons=b;
    status0=p[52];
    out->battery_percent=(uint8_t)(((status0 & 0x0fu) * 100u + 5u) / 10u);
    if (out->battery_percent > 100u) out->battery_percent=100u;
    out->battery_valid=1u;
    return 1;
}
int TitanGamepad_ParseDualSenseUsb(const uint8_t *data, size_t size, TitanGamepadProtocolResult *out)
{
    if (!data || size != 64u || data[0] != 0x01u) return 0;
    return parse_dualsense_common(data+1, size-1u, out);
}
int TitanGamepad_ParseDualSenseBluetooth(const uint8_t *data, size_t size, TitanGamepadProtocolResult *out)
{
    /* BT report 0x31 has a 2-byte transport header before the common 63-byte payload. */
    if (!data || size != 78u || data[0] != 0x31u) return 0;
    return parse_dualsense_common(data+2, 63u, out);
}
int TitanGamepad_ParseXboxGipInput(const uint8_t *p, size_t size, TitanGamepadProtocolResult *out)
{
    uint32_t b=0u;
    if (!p || !out || size < 14u) return 0;
    clear_result(out);
    /* Xbox GIP input payload (bytes after the 4-byte GIP header). */
    if (p[0]&0x04u) b|=TITAN_NBTN_START;
    if (p[0]&0x08u) b|=TITAN_NBTN_SELECT;
    if (p[0]&0x10u) b|=TITAN_NBTN_CROSS;    /* A */
    if (p[0]&0x20u) b|=TITAN_NBTN_CIRCLE;   /* B */
    if (p[0]&0x40u) b|=TITAN_NBTN_SQUARE;   /* X */
    if (p[0]&0x80u) b|=TITAN_NBTN_TRIANGLE; /* Y */
    if (p[1]&0x01u) b|=TITAN_NBTN_UP;
    if (p[1]&0x02u) b|=TITAN_NBTN_DOWN;
    if (p[1]&0x04u) b|=TITAN_NBTN_LEFT;
    if (p[1]&0x08u) b|=TITAN_NBTN_RIGHT;
    if (p[1]&0x10u) b|=TITAN_NBTN_L1;
    if (p[1]&0x20u) b|=TITAN_NBTN_R1;
    if (p[1]&0x40u) b|=TITAN_NBTN_L3;
    if (p[1]&0x80u) b|=TITAN_NBTN_R3;
    out->state.trigger_l=trig10_to_u8(le16(p+2));
    out->state.trigger_r=trig10_to_u8(le16(p+4));
    if (out->state.trigger_l) b|=TITAN_NBTN_L2;
    if (out->state.trigger_r) b|=TITAN_NBTN_R2;
    out->state.left_x=axis16_to_u8(les16(p+6));
    out->state.left_y=(uint8_t)(255u-axis16_to_u8(les16(p+8)));
    out->state.right_x=axis16_to_u8(les16(p+10));
    out->state.right_y=(uint8_t)(255u-axis16_to_u8(les16(p+12)));
    out->state.buttons=b;
    return 1;
}

int TitanGamepad_ParseXboxGipPacket(const uint8_t *packet, size_t size, TitanGamepadProtocolResult *out)
{
    /* GIP: command/options/sequence/payload_length + payload. */
    if (!packet || !out || size < 4u) return 0;
    if (packet[0] == 0x07u) { /* virtual key report: Xbox/Guide */
        if (size < 5u) return 0;
        clear_result(out);
        if (packet[4] & 0x03u) out->state.buttons |= TITAN_NBTN_SYSTEM;
        return 1;
    }
    if (packet[0] != 0x20u || packet[3] < 14u || size < 18u) return 0;
    return TitanGamepad_ParseXboxGipInput(packet + 4u, size - 4u, out);
}

int TitanGamepad_ParseXboxBluetoothHid(const uint8_t *p, size_t size, TitanGamepadProtocolResult *out)
{
    uint16_t buttons;
    uint16_t brake, accel;
    uint32_t b=0u;
    uint8_t hat;
    if (!p || !out || size < 17u || p[0] != 0x01u) return 0;
    clear_result(out);
    /* Unified BLE HID descriptor used by modern Xbox Wireless controllers. */
    out->state.left_x=(uint8_t)((le16(p+1) * 255u + 32767u) / 65535u);
    out->state.left_y=(uint8_t)(255u-((le16(p+3) * 255u + 32767u) / 65535u));
    out->state.right_x=(uint8_t)((le16(p+5) * 255u + 32767u) / 65535u);
    out->state.right_y=(uint8_t)(255u-((le16(p+7) * 255u + 32767u) / 65535u));
    brake=(uint16_t)(le16(p+9)&0x03ffu);
    accel=(uint16_t)(le16(p+11)&0x03ffu);
    out->state.trigger_l=trig10_to_u8(brake);
    out->state.trigger_r=trig10_to_u8(accel);
    if (out->state.trigger_l) b|=TITAN_NBTN_L2;
    if (out->state.trigger_r) b|=TITAN_NBTN_R2;
    hat=(uint8_t)(p[13]&0x0fu);
    /* HID hat is 1..8 clockwise; 0 is neutral/null. */
    if (hat >= 1u && hat <= 8u) map_hat((uint8_t)(hat-1u), &b);
    buttons=le16(p+14);
    if (buttons&(1u<<0)) b|=TITAN_NBTN_CROSS;    /* A */
    if (buttons&(1u<<1)) b|=TITAN_NBTN_CIRCLE;   /* B */
    if (buttons&(1u<<2)) b|=TITAN_NBTN_SQUARE;   /* X */
    if (buttons&(1u<<3)) b|=TITAN_NBTN_TRIANGLE; /* Y */
    if (buttons&(1u<<4)) b|=TITAN_NBTN_L1;
    if (buttons&(1u<<5)) b|=TITAN_NBTN_R1;
    if (buttons&(1u<<6)) b|=TITAN_NBTN_SELECT;   /* View */
    if (buttons&(1u<<7)) b|=TITAN_NBTN_START;    /* Menu */
    if (buttons&(1u<<8)) b|=TITAN_NBTN_L3;
    if (buttons&(1u<<9)) b|=TITAN_NBTN_R3;
    if (buttons&(1u<<10)) b|=TITAN_NBTN_SYSTEM;
    if (p[16]&0x01u) b|=TITAN_NBTN_TOUCHPAD; /* Share/Record normalized as auxiliary */
    out->state.buttons=b;
    return 1;
}
