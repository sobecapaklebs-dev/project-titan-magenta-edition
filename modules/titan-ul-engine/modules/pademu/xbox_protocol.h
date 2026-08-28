#ifndef TITAN_XBOX_PROTOCOL_H
#define TITAN_XBOX_PROTOCOL_H

#include <stdint.h>
#include <ds34common.h>

#define TITAN_XBOX_GIP_CMD_ACK         0x01
#define TITAN_XBOX_GIP_CMD_POWER       0x05
#define TITAN_XBOX_GIP_CMD_VIRTUAL_KEY 0x07
#define TITAN_XBOX_GIP_CMD_INPUT       0x20
#define TITAN_XBOX_GIP_OPT_ACK         0x10
#define TITAN_XBOX_GIP_OPT_INTERNAL    0x20

int titan_xbox_usb_pid_supported(uint16_t pid);
int titan_xbox_usb_pid_series(uint16_t pid);
int titan_xbox_usb_needs_s_init(uint16_t pid);
int titan_xbox_gip_build_power_on(uint8_t sequence, uint8_t *out, int capacity);
int titan_xbox_gip_build_s_init(uint8_t sequence, uint8_t *out, int capacity);
int titan_xbox_gip_build_virtual_key_ack(uint8_t sequence, uint8_t *out, int capacity);
int titan_xbox_gip_parse_ds2(const uint8_t *data, int bytes, struct ds2report *out, int *guide_pressed);
int titan_xbox_bt_hid_parse_ds2(const uint8_t *data, int bytes, struct ds2report *out, int *guide_pressed);

#endif
