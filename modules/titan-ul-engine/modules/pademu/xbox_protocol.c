#include "xbox_protocol.h"

static uint8_t axis_to_uint8_t(int16_t value, int invert)
{
    int32_t v = (int32_t)value + 32768;
    if (invert)
        v = 65535 - v;
    return (uint8_t)(v >> 8);
}

static uint8_t trigger_to_uint8_t(uint16_t value)
{
    if (value > 1023)
        value = 1023;
    return (uint8_t)((value * 255u + 511u) / 1023u);
}

static void set_button(struct ds2report *out, uint16_t bit, int pressed)
{
    if (pressed)
        out->nButtonState &= ~bit;
    else
        out->nButtonState |= bit;
}

int titan_xbox_usb_pid_supported(uint16_t pid)
{
    switch (pid) {
        case XBOX_ONE_PID:
        case XBOX_ONE_2015_PID:
        case XBOX_ONE_ELITE_PID:
        case XBOX_ONE_S_PID:
        case XBOX_ELITE2_PID:
        case XBOX_SERIES_PID:
            return 1;
        default:
            return 0;
    }
}

int titan_xbox_usb_pid_series(uint16_t pid)
{
    return pid == XBOX_SERIES_PID;
}

int titan_xbox_usb_needs_s_init(uint16_t pid)
{
    return pid == XBOX_ONE_S_PID || pid == XBOX_ELITE2_PID;
}

int titan_xbox_gip_build_power_on(uint8_t sequence, uint8_t *out, int capacity)
{
    if (out == 0 || capacity < 5)
        return 0;
    out[0] = TITAN_XBOX_GIP_CMD_POWER;
    out[1] = TITAN_XBOX_GIP_OPT_INTERNAL;
    out[2] = sequence;
    out[3] = 1;
    out[4] = 0;
    return 5;
}

int titan_xbox_gip_build_s_init(uint8_t sequence, uint8_t *out, int capacity)
{
    if (out == 0 || capacity < 5)
        return 0;
    out[0] = TITAN_XBOX_GIP_CMD_POWER;
    out[1] = TITAN_XBOX_GIP_OPT_INTERNAL;
    out[2] = sequence;
    out[3] = 0x0f;
    out[4] = 0x06;
    return 5;
}

int titan_xbox_gip_build_virtual_key_ack(uint8_t sequence, uint8_t *out, int capacity)
{
    static const uint8_t template_ack[13] = {
        TITAN_XBOX_GIP_CMD_ACK, TITAN_XBOX_GIP_OPT_INTERNAL, 0, 9,
        0, TITAN_XBOX_GIP_CMD_VIRTUAL_KEY,
        TITAN_XBOX_GIP_OPT_ACK | TITAN_XBOX_GIP_OPT_INTERNAL,
        0, 0, 0, 0, 0x80, 0
    };
    int i;
    if (out == 0 || capacity < (int)sizeof(template_ack))
        return 0;
    for (i = 0; i < (int)sizeof(template_ack); i++)
        out[i] = template_ack[i];
    out[2] = sequence;
    out[7] = sequence;
    return sizeof(template_ack);
}

int titan_xbox_gip_parse_ds2(const uint8_t *data, int bytes, struct ds2report *out, int *guide_pressed)
{
    const uint8_t *p;
    uint16_t lt, rt;
    int16_t lx, ly, rx, ry;
    if (guide_pressed)
        *guide_pressed = 0;
    if (data == 0 || out == 0 || bytes < 5)
        return 0;
    if (data[0] == TITAN_XBOX_GIP_CMD_VIRTUAL_KEY) {
        if (guide_pressed)
            *guide_pressed = (data[4] & 0x03) != 0;
        return 1;
    }
    if (data[0] != TITAN_XBOX_GIP_CMD_INPUT || bytes < 18 || data[3] < 14)
        return 0;
    p = &data[4];
    out->nButtonState = 0xffff;
    set_button(out, DS2ButtonStart,    p[0] & 0x04);
    set_button(out, DS2ButtonSelect,   p[0] & 0x08);
    set_button(out, DS2ButtonCross,    p[0] & 0x10);
    set_button(out, DS2ButtonCircle,   p[0] & 0x20);
    set_button(out, DS2ButtonSquare,   p[0] & 0x40);
    set_button(out, DS2ButtonTriangle, p[0] & 0x80);
    set_button(out, DS2ButtonUp,       p[1] & 0x01);
    set_button(out, DS2ButtonDown,     p[1] & 0x02);
    set_button(out, DS2ButtonLeft,     p[1] & 0x04);
    set_button(out, DS2ButtonRight,    p[1] & 0x08);
    set_button(out, DS2ButtonL1,       p[1] & 0x10);
    set_button(out, DS2ButtonR1,       p[1] & 0x20);
    set_button(out, DS2ButtonL3,       p[1] & 0x40);
    set_button(out, DS2ButtonR3,       p[1] & 0x80);
    lt = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
    rt = (uint16_t)p[4] | ((uint16_t)p[5] << 8);
    lx = (int16_t)((uint16_t)p[6] | ((uint16_t)p[7] << 8));
    ly = (int16_t)((uint16_t)p[8] | ((uint16_t)p[9] << 8));
    rx = (int16_t)((uint16_t)p[10] | ((uint16_t)p[11] << 8));
    ry = (int16_t)((uint16_t)p[12] | ((uint16_t)p[13] << 8));
    out->LeftStickX = axis_to_uint8_t(lx, 0);
    out->LeftStickY = axis_to_uint8_t(ly, 1);
    out->RightStickX = axis_to_uint8_t(rx, 0);
    out->RightStickY = axis_to_uint8_t(ry, 1);
    out->PressureL2 = trigger_to_uint8_t(lt);
    out->PressureR2 = trigger_to_uint8_t(rt);
    set_button(out, DS2ButtonL2, out->PressureL2 != 0);
    set_button(out, DS2ButtonR2, out->PressureR2 != 0);
    out->PressureUp = (out->nButtonState & DS2ButtonUp) ? 0 : 0xff;
    out->PressureDown = (out->nButtonState & DS2ButtonDown) ? 0 : 0xff;
    out->PressureLeft = (out->nButtonState & DS2ButtonLeft) ? 0 : 0xff;
    out->PressureRight = (out->nButtonState & DS2ButtonRight) ? 0 : 0xff;
    out->PressureCross = (out->nButtonState & DS2ButtonCross) ? 0 : 0xff;
    out->PressureCircle = (out->nButtonState & DS2ButtonCircle) ? 0 : 0xff;
    out->PressureSquare = (out->nButtonState & DS2ButtonSquare) ? 0 : 0xff;
    out->PressureTriangle = (out->nButtonState & DS2ButtonTriangle) ? 0 : 0xff;
    out->PressureL1 = (out->nButtonState & DS2ButtonL1) ? 0 : 0xff;
    out->PressureR1 = (out->nButtonState & DS2ButtonR1) ? 0 : 0xff;
    return 1;
}


static void map_hat_ds2(uint8_t hat, struct ds2report *out)
{
    int up=0,right=0,down=0,left=0;
    if (hat >= 1 && hat <= 8) {
        switch (hat) {
            case 1: up=1; break; case 2: up=right=1; break; case 3: right=1; break;
            case 4: right=down=1; break; case 5: down=1; break; case 6: down=left=1; break;
            case 7: left=1; break; case 8: left=up=1; break;
        }
    }
    set_button(out, DS2ButtonUp, up); set_button(out, DS2ButtonRight, right);
    set_button(out, DS2ButtonDown, down); set_button(out, DS2ButtonLeft, left);
}

int titan_xbox_bt_hid_parse_ds2(const uint8_t *p, int bytes, struct ds2report *out, int *guide_pressed)
{
    uint16_t buttons, lt, rt, lx, ly, rx, ry;
    if (guide_pressed) *guide_pressed = 0;
    if (!p || !out || bytes < 17 || p[0] != 0x01) return 0;
    out->nButtonState=0xffff;
    lx=(uint16_t)p[1]|((uint16_t)p[2]<<8); ly=(uint16_t)p[3]|((uint16_t)p[4]<<8);
    rx=(uint16_t)p[5]|((uint16_t)p[6]<<8); ry=(uint16_t)p[7]|((uint16_t)p[8]<<8);
    lt=((uint16_t)p[9]|((uint16_t)p[10]<<8))&0x03ff; rt=((uint16_t)p[11]|((uint16_t)p[12]<<8))&0x03ff;
    out->LeftStickX=(uint8_t)(((uint32_t)lx*255u+32767u)/65535u);
    out->LeftStickY=(uint8_t)(255u-(((uint32_t)ly*255u+32767u)/65535u));
    out->RightStickX=(uint8_t)(((uint32_t)rx*255u+32767u)/65535u);
    out->RightStickY=(uint8_t)(255u-(((uint32_t)ry*255u+32767u)/65535u));
    out->PressureL2=trigger_to_uint8_t(lt); out->PressureR2=trigger_to_uint8_t(rt);
    set_button(out,DS2ButtonL2,out->PressureL2!=0); set_button(out,DS2ButtonR2,out->PressureR2!=0);
    map_hat_ds2((uint8_t)(p[13]&0x0f),out); buttons=(uint16_t)p[14]|((uint16_t)p[15]<<8);
    set_button(out,DS2ButtonCross,buttons&(1u<<0)); set_button(out,DS2ButtonCircle,buttons&(1u<<1));
    set_button(out,DS2ButtonSquare,buttons&(1u<<2)); set_button(out,DS2ButtonTriangle,buttons&(1u<<3));
    set_button(out,DS2ButtonL1,buttons&(1u<<4)); set_button(out,DS2ButtonR1,buttons&(1u<<5));
    set_button(out,DS2ButtonSelect,buttons&(1u<<6)); set_button(out,DS2ButtonStart,buttons&(1u<<7));
    set_button(out,DS2ButtonL3,buttons&(1u<<8)); set_button(out,DS2ButtonR3,buttons&(1u<<9));
    if (guide_pressed) *guide_pressed=(buttons&(1u<<10))!=0;
    out->PressureUp=(out->nButtonState&DS2ButtonUp)?0:0xff; out->PressureDown=(out->nButtonState&DS2ButtonDown)?0:0xff;
    out->PressureLeft=(out->nButtonState&DS2ButtonLeft)?0:0xff; out->PressureRight=(out->nButtonState&DS2ButtonRight)?0:0xff;
    out->PressureCross=(out->nButtonState&DS2ButtonCross)?0:0xff; out->PressureCircle=(out->nButtonState&DS2ButtonCircle)?0:0xff;
    out->PressureSquare=(out->nButtonState&DS2ButtonSquare)?0:0xff; out->PressureTriangle=(out->nButtonState&DS2ButtonTriangle)?0:0xff;
    out->PressureL1=(out->nButtonState&DS2ButtonL1)?0:0xff; out->PressureR1=(out->nButtonState&DS2ButtonR1)?0:0xff;
    return 1;
}
