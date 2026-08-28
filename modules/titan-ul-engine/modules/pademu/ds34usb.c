#include "types.h"
#include "loadcore.h"
#include "stdio.h"
#include "sifrpc.h"
#include "sysclib.h"
#include "usbd.h"
#include "usbd_macro.h"
#include "thbase.h"
#include "thsemap.h"
#include "ds34usb.h"
#include "sys_utils.h"
#include "padmacro.h"
#include "xbox_protocol.h"
#include <titan_pademu_telemetry.h>
#include "pademu_telemetry_rpc.h"

// #define DPRINTF(x...) printf(x)
#define DPRINTF(x...)

#define REQ_USB_OUT (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE)
#define REQ_USB_IN  (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE)

#define MAX_PADS 4

static u8 output_01_report[] =
    {
        0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x02,
        0xff, 0x27, 0x10, 0x00, 0x32,
        0xff, 0x27, 0x10, 0x00, 0x32,
        0xff, 0x27, 0x10, 0x00, 0x32,
        0xff, 0x27, 0x10, 0x00, 0x32,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00};

static u8 led_patterns[][2] =
    {
        {0x1C, 0x02},
        {0x1A, 0x04},
        {0x16, 0x08},
        {0x0E, 0x10},
};

static u8 power_level[] =
    {
        0x00, 0x00, 0x02, 0x06, 0x0E, 0x1E};

static u8 rgbled_patterns[][2][3] =
    {
        {{0x00, 0x00, 0x10}, {0x00, 0x00, 0x7F}}, // light blue/blue
        {{0x00, 0x10, 0x00}, {0x00, 0x7F, 0x00}}, // light green/green
        {{0x10, 0x10, 0x00}, {0x7F, 0x7F, 0x00}}, // light yellow/yellow
        {{0x00, 0x10, 0x10}, {0x00, 0x7F, 0x7F}}, // light cyan/cyan
};

static u8 usb_buf[MAX_BUFFER_SIZE + 32] __attribute((aligned(4))) = {0};

int usb_probe(int devId);
int usb_connect(int devId);
int usb_disconnect(int devId);

static void usb_release(int pad);
static void usb_config_set(int result, int count, void *arg);

UsbDriver usb_driver = {NULL, NULL, "ds34usb", usb_probe, usb_connect, usb_disconnect};

static void DS3USB_init(int pad);
static void readReport(u8 *data, int pad);
static int LEDRumble(u8 *led, u8 lrum, u8 rrum, int pad);
static void TransferWait(int sema);
static int xbox_send_init(int pad);
static int xbox_send_ack(int pad, u8 seq);
static void telemetry_battery_ds3(ds34usb_device *pad, u8 raw)
{
    static const u8 pct[6] = {0,1,25,50,75,100};
    pad->battery_raw = raw;
    if (raw == 0xEE) { pad->battery_state=TITAN_PADEMU_BATTERY_CHARGING; pad->battery_valid=0; pad->battery_percent=0; }
    else if (raw == 0xEF) { pad->battery_state=TITAN_PADEMU_BATTERY_FULL; pad->battery_valid=1; pad->battery_percent=100; }
    else if (raw <= 5) { pad->battery_state=TITAN_PADEMU_BATTERY_LEVEL; pad->battery_valid=1; pad->battery_percent=pct[raw]; }
    else { pad->battery_state=TITAN_PADEMU_BATTERY_UNKNOWN; pad->battery_valid=0; pad->battery_percent=0; }
}
static void telemetry_battery_ds4(ds34usb_device *pad, const struct ds4report *r)
{
    u8 cap=r->Power;
    pad->battery_raw=(u8)((r->Usb_plugged?0x10:0)|cap);
    if (cap==0x0B) { pad->battery_state=TITAN_PADEMU_BATTERY_FULL; pad->battery_percent=100; pad->battery_valid=1; }
    else if (cap<=10) { pad->battery_state=r->Usb_plugged?TITAN_PADEMU_BATTERY_CHARGING:TITAN_PADEMU_BATTERY_LEVEL; pad->battery_percent=(u8)(cap*10); pad->battery_valid=1; }
    else { pad->battery_state=TITAN_PADEMU_BATTERY_UNKNOWN; pad->battery_percent=0; pad->battery_valid=0; }
}
static void telemetry_battery_dualsense(ds34usb_device *pad, const struct dualsensereport *r)
{
    u8 cap=(u8)(r->Status0 & 0x0f), st=(u8)(r->Status0 >> 4);
    pad->battery_raw=r->Status0;
    if (cap>10) { pad->battery_state=TITAN_PADEMU_BATTERY_UNKNOWN; pad->battery_valid=0; pad->battery_percent=0; return; }
    pad->battery_percent=(u8)(cap*10); pad->battery_valid=1;
    if (st==1) pad->battery_state=TITAN_PADEMU_BATTERY_CHARGING;
    else if (st==2) { pad->battery_state=TITAN_PADEMU_BATTERY_FULL; pad->battery_percent=100; }
    else pad->battery_state=TITAN_PADEMU_BATTERY_LEVEL;
}

ds34usb_device ds34pad[MAX_PADS];

int usb_probe(int devId)
{
    UsbDeviceDescriptor *device = NULL;

    DPRINTF("DS34USB: probe: devId=%i\n", devId);

    device = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    if (device == NULL) {
        DPRINTF("DS34USB: Error - Couldn't get device descriptor\n");
        return 0;
    }

    if (device->idVendor == SONY_VID && (device->idProduct == GUITAR_HERO_PS3_PID || device->idProduct == ROCK_BAND_PS3_PID)) {
        return 1;
    }

    if (device->idVendor == DS34_VID && (device->idProduct == DS3_PID || device->idProduct == DS4_PID || device->idProduct == DS4_PID_SLIM || device->idProduct == DUALSENSE_PID || device->idProduct == DUALSENSE_EDGE_PID))
        return 1;

    if (device->idVendor == MICROSOFT_VID && titan_xbox_usb_pid_supported(device->idProduct))
        return 1;

    return 0;
}

int usb_connect(int devId)
{
    int pad;
    unsigned char *cursor;
    unsigned char *config_end;
    UsbDeviceDescriptor *device = NULL;
    UsbConfigDescriptor *config = NULL;
    UsbInterfaceDescriptor *interface = NULL;

    DPRINTF("DS34USB: connect: devId=%i\n", devId);

    for (pad = 0; pad < MAX_PADS; pad++) {
        if (ds34pad[pad].devId == -1 && ds34pad[pad].enabled)
            break;
    }

    if (pad >= MAX_PADS) {
        DPRINTF("DS34USB: Error - only %d device allowed !\n", MAX_PADS);
        return 1;
    }

    PollSema(ds34pad[pad].sema);

    /* Do not publish a device into the pad slot until every descriptor needed
     * by the transport has been validated.  The old code dereferenced device
     * before UsbGetDeviceStaticDescriptor() assigned it, which is undefined
     * behaviour on the IOP and was caught by the first real BF30 target build. */
    device = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    if (device == NULL || device->bNumConfigurations < 1) {
        DPRINTF("DS34USB: Error - invalid device descriptor\n");
        SignalSema(ds34pad[pad].sema);
        return 1;
    }

    config = (UsbConfigDescriptor *)UsbGetDeviceStaticDescriptor(devId, device, USB_DT_CONFIG);
    if (config == NULL || config->bLength < 2u || config->wTotalLength < (unsigned int)(config->bLength + 2u)) {
        DPRINTF("DS34USB: Error - invalid configuration descriptor\n");
        SignalSema(ds34pad[pad].sema);
        return 1;
    }

    interface = (UsbInterfaceDescriptor *)((unsigned char *)config + config->bLength);
    if ((unsigned char *)interface + 2u > (unsigned char *)config + config->wTotalLength ||
        interface->bLength < 2u || interface->bDescriptorType != USB_DT_INTERFACE) {
        DPRINTF("DS34USB: Error - invalid interface descriptor\n");
        SignalSema(ds34pad[pad].sema);
        return 1;
    }

    ds34pad[pad].controlEndp = UsbOpenEndpoint(devId, NULL);
    if (ds34pad[pad].controlEndp < 0) {
        DPRINTF("DS34USB: Error - couldn't open control endpoint\n");
        SignalSema(ds34pad[pad].sema);
        return 1;
    }

    ds34pad[pad].devId = devId;
    ds34pad[pad].vendor_id = device->idVendor;
    ds34pad[pad].product_id = device->idProduct;
    ds34pad[pad].telemetry_generation++;
    ds34pad[pad].system_button = 0;
    ds34pad[pad].battery_state = TITAN_PADEMU_BATTERY_UNSUPPORTED;
    ds34pad[pad].battery_valid = 0;
    ds34pad[pad].battery_percent = 0;
    ds34pad[pad].battery_raw = 0;
    ds34pad[pad].status = DS34USB_STATE_AUTHORIZED;

    if (device->idProduct == DS3_PID) {
        ds34pad[pad].type = DS3;
    } else if (device->idProduct == GUITAR_HERO_PS3_PID) {
        ds34pad[pad].type = GUITAR_GH;
    } else if (device->idProduct == ROCK_BAND_PS3_PID) {
        ds34pad[pad].type = GUITAR_RB;
    } else if (device->idVendor == MICROSOFT_VID && titan_xbox_usb_pid_supported(device->idProduct)) {
        ds34pad[pad].type = titan_xbox_usb_pid_series(device->idProduct) ? XBOX_SERIES : XBOX_ONE;
    } else if (device->idProduct == DUALSENSE_PID || device->idProduct == DUALSENSE_EDGE_PID) {
        ds34pad[pad].type = DUALSENSE;
    } else {
        ds34pad[pad].type = DS4;
    }

    /* Walk only the descriptors that belong to this interface and stay inside
     * wTotalLength.  This keeps DS4-v2 compatibility (which can report zero
     * endpoints in bNumEndpoints) without the old arbitrary 20-descriptor walk
     * that could advance beyond the configuration descriptor. */
    cursor = (unsigned char *)interface + interface->bLength;
    config_end = (unsigned char *)config + config->wTotalLength;
    while (cursor + 2u <= config_end) {
        unsigned int descriptor_length = cursor[0];
        unsigned int descriptor_type = cursor[1];
        UsbEndpointDescriptor *endpoint;

        if (descriptor_length < 2u || cursor + descriptor_length > config_end)
            break;
        if (descriptor_type == USB_DT_INTERFACE)
            break;
        if (descriptor_type != USB_DT_ENDPOINT || descriptor_length < 7u) {
            cursor += descriptor_length;
            continue;
        }

        endpoint = (UsbEndpointDescriptor *)cursor;
        if (endpoint->bmAttributes == USB_ENDPOINT_XFER_INT) {
            if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN && ds34pad[pad].interruptEndp < 0) {
                ds34pad[pad].interruptEndp = UsbOpenEndpointAligned(devId, endpoint);
                DPRINTF("DS34USB: register Event endpoint id =%i addr=%02X packetSize=%i\n", ds34pad[pad].interruptEndp, endpoint->bEndpointAddress, (unsigned short int)endpoint->wMaxPacketSizeHB << 8 | endpoint->wMaxPacketSizeLB);
            }
            if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT && ds34pad[pad].outEndp < 0) {
                ds34pad[pad].outEndp = UsbOpenEndpointAligned(devId, endpoint);
                DPRINTF("DS34USB: register Output endpoint id =%i addr=%02X packetSize=%i\n", ds34pad[pad].outEndp, endpoint->bEndpointAddress, (unsigned short int)endpoint->wMaxPacketSizeHB << 8 | endpoint->wMaxPacketSizeLB);
            }
        }
        cursor += descriptor_length;
    }

    if (ds34pad[pad].interruptEndp < 0 || ds34pad[pad].outEndp < 0) {
        /* We already own the semaphore.  Calling usb_release() here used to
         * PollSema() the same mutex again and could deadlock forever. */
        if (ds34pad[pad].interruptEndp >= 0) UsbCloseEndpoint(ds34pad[pad].interruptEndp);
        if (ds34pad[pad].outEndp >= 0) UsbCloseEndpoint(ds34pad[pad].outEndp);
        if (ds34pad[pad].controlEndp >= 0) UsbCloseEndpoint(ds34pad[pad].controlEndp);
        ds34pad[pad].controlEndp = -1;
        ds34pad[pad].interruptEndp = -1;
        ds34pad[pad].outEndp = -1;
        ds34pad[pad].devId = -1;
        ds34pad[pad].status = DS34USB_STATE_DISCONNECTED;
        SignalSema(ds34pad[pad].sema);
        return 1;
    }

    ds34pad[pad].status |= DS34USB_STATE_CONNECTED;
    UsbSetDeviceConfiguration(ds34pad[pad].controlEndp, config->bConfigurationValue, usb_config_set, (void *)pad);
    SignalSema(ds34pad[pad].sema);

    return 0;
}

int usb_disconnect(int devId)
{
    u8 pad;

    DPRINTF("DS34USB: disconnect: devId=%i\n", devId);

    for (pad = 0; pad < MAX_PADS; pad++) {
        if (ds34pad[pad].devId == devId)
            break;
    }

    if (pad < MAX_PADS)
        usb_release(pad);

    return 0;
}

static void usb_release(int pad)
{
    PollSema(ds34pad[pad].sema);

    if (ds34pad[pad].interruptEndp >= 0)
        UsbCloseEndpoint(ds34pad[pad].interruptEndp);

    if (ds34pad[pad].outEndp >= 0)
        UsbCloseEndpoint(ds34pad[pad].outEndp);

    if (ds34pad[pad].controlEndp >= 0)
        UsbCloseEndpoint(ds34pad[pad].controlEndp);

    ds34pad[pad].controlEndp = -1;
    ds34pad[pad].interruptEndp = -1;
    ds34pad[pad].outEndp = -1;
    ds34pad[pad].devId = -1;
    ds34pad[pad].status = DS34USB_STATE_DISCONNECTED;

    SignalSema(ds34pad[pad].sema);
}

static int usb_resulCode;

static void usb_data_cb(int resultCode, int bytes, void *arg)
{
    int pad = (int)arg;

    // DPRINTF("DS34USB: usb_data_cb: res %d, bytes %d, arg %p \n", resultCode, bytes, arg);

    usb_resulCode = resultCode;

    SignalSema(ds34pad[pad].sema);
}

static void usb_cmd_cb(int resultCode, int bytes, void *arg)
{
    int pad = (int)arg;

    // DPRINTF("DS34USB: usb_cmd_cb: res %d, bytes %d, arg %p \n", resultCode, bytes, arg);

    SignalSema(ds34pad[pad].cmd_sema);
}

static void usb_config_set(int result, int count, void *arg)
{
    int pad = (int)arg;
    u8 led[4];

    PollSema(ds34pad[pad].sema);

    ds34pad[pad].status |= DS34USB_STATE_CONFIGURED;

    if (ds34pad[pad].type == DS3) {
        DS3USB_init(pad);
        DelayThread(10000);
        led[0] = led_patterns[pad][1];
        led[3] = 0;
    } else if (ds34pad[pad].type == DS4) {
        led[0] = rgbled_patterns[pad][1][0];
        led[1] = rgbled_patterns[pad][1][1];
        led[2] = rgbled_patterns[pad][1][2];
        led[3] = 0;
    }

    if (ds34pad[pad].type == DS3 || ds34pad[pad].type == DS4)
        LEDRumble(led, 0, 0, pad);
    else if (ds34pad[pad].type == XBOX_ONE || ds34pad[pad].type == XBOX_SERIES) {
        led[0] = led[1] = led[2] = led[3] = 0;
        ds34pad[pad].update_rum = 0;
        xbox_send_init(pad);
    } else {
        led[0] = led[1] = led[2] = led[3] = 0;
        ds34pad[pad].update_rum = 0;
    }

    ds34pad[pad].status |= DS34USB_STATE_RUNNING;

    SignalSema(ds34pad[pad].sema);
}

static void DS3USB_init(int pad)
{
    usb_buf[0] = 0x42;
    usb_buf[1] = 0x0c;
    usb_buf[2] = 0x00;
    usb_buf[3] = 0x00;

    UsbControlTransfer(ds34pad[pad].controlEndp, REQ_USB_OUT, USB_REQ_SET_REPORT, (HID_USB_GET_REPORT_FEATURE << 8) | 0xF4, 0, 4, usb_buf, NULL, NULL);
}

static int xbox_send_packet(int pad, const u8 *packet, int size)
{
    int ret;
    if (ds34pad[pad].outEndp < 0 || size <= 0 || size > (int)sizeof(usb_buf))
        return -1;
    PollSema(ds34pad[pad].cmd_sema);
    mips_memset(usb_buf, 0, sizeof(usb_buf));
    mips_memcpy(usb_buf, packet, size);
    ret = UsbInterruptTransfer(ds34pad[pad].outEndp, usb_buf, size, usb_cmd_cb, (void *)pad);
    if (ret == USB_RC_OK)
        TransferWait(ds34pad[pad].cmd_sema);
    else
        SignalSema(ds34pad[pad].cmd_sema);
    return ret;
}

static int xbox_send_init(int pad)
{
    UsbDeviceDescriptor *device;
    u8 packet[16];
    int size, ret;
    device = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(ds34pad[pad].devId, NULL, USB_DT_DEVICE);
    if (device == NULL)
        return -1;
    size = titan_xbox_gip_build_power_on(1, packet, sizeof(packet));
    ret = xbox_send_packet(pad, packet, size);
    if (ret != USB_RC_OK)
        return ret;
    if (titan_xbox_usb_needs_s_init(device->idProduct)) {
        size = titan_xbox_gip_build_s_init(2, packet, sizeof(packet));
        ret = xbox_send_packet(pad, packet, size);
    }
    return ret;
}

static int xbox_send_ack(int pad, u8 seq)
{
    u8 ack[16];
    int size = titan_xbox_gip_build_virtual_key_ack(seq, ack, sizeof(ack));
    return xbox_send_packet(pad, ack, size);
}

static void xbox_translate_gip(const u8 *data, int bytes, int pad)
{
    int guide = 0;
    if (!titan_xbox_gip_parse_ds2(data, bytes, &ds34pad[pad].ds2, &guide))
        return;
    ds34pad[pad].system_button = guide ? 1 : 0;
    pademu_telemetry_system_button((unsigned)pad,ds34pad[pad].type,ds34pad[pad].system_button);
    ds34pad[pad].battery_state = TITAN_PADEMU_BATTERY_UNSUPPORTED;
    ds34pad[pad].battery_valid = 0;
    ds34pad[pad].telemetry_packets++;
    if (data[0] == TITAN_XBOX_GIP_CMD_VIRTUAL_KEY) {
        if ((data[1] & (TITAN_XBOX_GIP_OPT_ACK | TITAN_XBOX_GIP_OPT_INTERNAL)) ==
            (TITAN_XBOX_GIP_OPT_ACK | TITAN_XBOX_GIP_OPT_INTERNAL))
            xbox_send_ack(pad, data[2]);
        padMacroPerform(&ds34pad[pad].ds2, guide);
    }
}

#define MAX_DELAY 10

static void readReport(u8 *data, int pad_idx)
{
    ds34usb_device *pad = &ds34pad[pad_idx];
    if (pad->type == GUITAR_GH || pad->type == GUITAR_RB) {
        struct ds3guitarreport *report;

        report = (struct ds3guitarreport *)data;

        translate_pad_guitar(report, &pad->ds2, pad->type == GUITAR_GH);
        padMacroPerform(&pad->ds2, report->PSButton);
    }
    if (data[0]) {

        if (pad->type == DS3) {
            struct ds3report *report;

            report = (struct ds3report *)&data[2];

            if (report->RightStickX == 0 && report->RightStickY == 0) // ledrumble cmd causes null report sometime
                return;

            pad->data[0] = ~report->ButtonStateL;
            pad->data[1] = ~report->ButtonStateH;

            translate_pad_ds3(report, &pad->ds2, 0);
            pad->system_button = report->PSButton ? 1 : 0;
            pademu_telemetry_system_button((unsigned)pad_idx, pad->type, pad->system_button);
            telemetry_battery_ds3(pad, report->Power);
            pad->telemetry_packets++;
            padMacroPerform(&pad->ds2, report->PSButton);
            if (report->PSButton) {                                    // display battery level
                if (report->Select && (pad->btn_delay == MAX_DELAY)) { // PS + SELECT
                    if (pad->analog_btn < 2)                           // unlocked mode
                        pad->analog_btn = !pad->analog_btn;

                    pad->oldled[0] = led_patterns[pad_idx][(pad->analog_btn & 1)];
                    pad->btn_delay = 1;
                } else {
                    if (report->Power <= 0x05)
                        pad->oldled[0] = power_level[report->Power];

                    if (pad->btn_delay < MAX_DELAY)
                        pad->btn_delay++;
                }
            } else {
                pad->oldled[0] = led_patterns[pad_idx][(pad->analog_btn & 1)];

                if (pad->btn_delay > 0)
                    pad->btn_delay--;
            }

            if (report->Power == 0xEE) // charging
                pad->oldled[3] = 1;
            else
                pad->oldled[3] = 0;

        } else if (pad->type == XBOX_ONE || pad->type == XBOX_SERIES) {
            xbox_translate_gip(data, MAX_BUFFER_SIZE, pad_idx);
            pad->update_rum = 0;
        } else if (pad->type == DUALSENSE) {
            struct dualsensereport *report = (struct dualsensereport *)data;
            if (report->ReportID != 0x01)
                return;
            translate_pad_dualsense(report, &pad->ds2);
            pad->system_button = report->PSButton ? 1 : 0;
            pademu_telemetry_system_button((unsigned)pad_idx, pad->type, pad->system_button);
            telemetry_battery_dualsense(pad, report);
            pad->telemetry_packets++;
            padMacroPerform(&pad->ds2, report->PSButton);
            pad->update_rum = 0;
        } else if (pad->type == DS4) {
            struct ds4report *report;
            report = (struct ds4report *)data;
            translate_pad_ds4(report, &pad->ds2, 1);
            pad->system_button = report->PSButton ? 1 : 0;
            pademu_telemetry_system_button((unsigned)pad_idx, pad->type, pad->system_button);
            telemetry_battery_ds4(pad, report);
            pad->telemetry_packets++;
            padMacroPerform(&pad->ds2, report->PSButton);

            if (report->PSButton) {                                   // display battery level
                if (report->Share && (pad->btn_delay == MAX_DELAY)) { // PS + Share
                    if (pad->analog_btn < 2)                          // unlocked mode
                        pad->analog_btn = !pad->analog_btn;

                    pad->oldled[0] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][0];
                    pad->oldled[1] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][1];
                    pad->oldled[2] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][2];
                    pad->btn_delay = 1;
                } else {
                    pad->oldled[0] = report->Battery;
                    pad->oldled[1] = 0;
                    pad->oldled[2] = 0;

                    if (pad->btn_delay < MAX_DELAY)
                        pad->btn_delay++;
                }
            } else {
                pad->oldled[0] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][0];
                pad->oldled[1] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][1];
                pad->oldled[2] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][2];

                if (pad->btn_delay > 0)
                    pad->btn_delay--;
            }

            if (report->Power != 0xB && report->Usb_plugged) // charging
                pad->oldled[3] = 1;
            else
                pad->oldled[3] = 0;
        }
        if (pad->btn_delay > 0) {
            pad->update_rum = 1;
        }
    }
}

static int LEDRumble(u8 *led, u8 lrum, u8 rrum, int pad)
{
    int ret = 0;

    PollSema(ds34pad[pad].cmd_sema);

    mips_memset(usb_buf, 0, sizeof(usb_buf));

    if (ds34pad[pad].type == DS3) {
        mips_memcpy(usb_buf, output_01_report, sizeof(output_01_report));

        usb_buf[1] = 0xFE; // rt
        usb_buf[2] = rrum; // rp
        usb_buf[3] = 0xFE; // lt
        usb_buf[4] = lrum; // lp

        usb_buf[9] = led[0] & 0x7F; // LED Conf

        if (led[3]) // means charging, so blink
        {
            usb_buf[13] = 0x32;
            usb_buf[18] = 0x32;
            usb_buf[23] = 0x32;
            usb_buf[28] = 0x32;
        }

        ret = UsbControlTransfer(ds34pad[pad].controlEndp, REQ_USB_OUT, USB_REQ_SET_REPORT, (HID_USB_SET_REPORT_OUTPUT << 8) | 0x01, 0, sizeof(output_01_report), usb_buf, usb_cmd_cb, (void *)pad);
    } else if (ds34pad[pad].type == DS4) {
        usb_buf[0] = 0x05;
        usb_buf[1] = 0xFF;

        usb_buf[4] = rrum * 255; // ds4 has full control
        usb_buf[5] = lrum;

        usb_buf[6] = led[0]; // r
        usb_buf[7] = led[1]; // g
        usb_buf[8] = led[2]; // b

        if (led[3]) // means charging, so blink
        {
            usb_buf[9] = 0x80;  // Time to flash bright (255 = 2.5 seconds)
            usb_buf[10] = 0x80; // Time to flash dark (255 = 2.5 seconds)
        }

        ret = UsbInterruptTransfer(ds34pad[pad].outEndp, usb_buf, 32, usb_cmd_cb, (void *)pad);
    }

    ds34pad[pad].oldled[0] = led[0];
    ds34pad[pad].oldled[1] = led[1];
    ds34pad[pad].oldled[2] = led[2];
    ds34pad[pad].oldled[3] = led[3];

    return ret;
}

static unsigned int timeout(void *arg)
{
    int sema = (int)arg;
    iSignalSema(sema);
    return 0;
}

static void TransferWait(int sema)
{
    iop_sys_clock_t cmd_timeout;

    cmd_timeout.lo = 200000;
    cmd_timeout.hi = 0;

    if (SetAlarm(&cmd_timeout, timeout, (void *)sema) == 0) {
        WaitSema(sema);
        CancelAlarm(timeout, NULL);
    }
}

void ds34usb_set_rumble(u8 lrum, u8 rrum, int port)
{
    WaitSema(ds34pad[port].sema);

    ds34pad[port].update_rum = 1;
    ds34pad[port].lrum = lrum;
    ds34pad[port].rrum = rrum;

    SignalSema(ds34pad[port].sema);
}

int ds34usb_get_data(u8 *dst, int size, int port)
{
    int ret = 0;

    WaitSema(ds34pad[port].sema);

    PollSema(ds34pad[port].sema);

    ret = UsbInterruptTransfer(ds34pad[port].interruptEndp, usb_buf, MAX_BUFFER_SIZE, usb_data_cb, (void *)port);

    if (ret == USB_RC_OK) {
        TransferWait(ds34pad[port].sema);
        if (!usb_resulCode)
            readReport(usb_buf, port);

        usb_resulCode = 1;
    } else {
        DPRINTF("DS34USB: ds34usb_get_data usb transfer error %d\n", ret);
    }

    mips_memcpy(dst, ds34pad[port].data, size);
    ret = ds34pad[port].analog_btn & 1;

    if (ds34pad[port].update_rum) {
        ret = LEDRumble(ds34pad[port].oldled, ds34pad[port].lrum, ds34pad[port].rrum, port);
        if (ret == USB_RC_OK)
            TransferWait(ds34pad[port].cmd_sema);
        else
            DPRINTF("DS34USB: LEDRumble usb transfer error %d\n", ret);

        ds34pad[port].update_rum = 0;
    }

    SignalSema(ds34pad[port].sema);

    return ret;
}

void ds34usb_set_mode(int mode, int lock, int port)
{
    if (lock == 3)
        ds34pad[port].analog_btn = 3;
    else
        ds34pad[port].analog_btn = mode;
}

void ds34usb_reset()
{
    int pad;

    for (pad = 0; pad < MAX_PADS; pad++)
        usb_release(pad);
}

int ds34usb_get_telemetry(int port, void *out)
{
    TitanPademuPadTelemetry *t=(TitanPademuPadTelemetry *)out;
    ds34usb_device *p;
    if (!t || port<0 || port>=MAX_PADS) return 0;
    p=&ds34pad[port]; mips_memset(t,0,sizeof(*t));
    t->port=(u8)port; t->transport=TITAN_PADEMU_TRANSPORT_USB;
    t->connected=(p->status & DS34USB_STATE_RUNNING)?1:0;
    t->system_button=p->system_button; t->battery_state=p->battery_state;
    t->battery_percent=p->battery_percent; t->battery_valid=p->battery_valid; t->battery_raw=p->battery_raw;
    t->vendor_id=p->vendor_id; t->product_id=p->product_id; t->generation=p->telemetry_generation; t->packets=p->telemetry_packets;
    t->identity_quality=1; t->stable_key=((u32)p->vendor_id<<16) ^ (u32)p->product_id ^ ((u32)port<<8);
    switch(p->type){case DS3:t->device_type=TITAN_PADEMU_DEVICE_DS3;break;case DS4:t->device_type=TITAN_PADEMU_DEVICE_DS4;break;case DUALSENSE:t->device_type=TITAN_PADEMU_DEVICE_DUALSENSE;break;case XBOX_ONE:t->device_type=TITAN_PADEMU_DEVICE_XBOX_ONE;break;case XBOX_SERIES:t->device_type=TITAN_PADEMU_DEVICE_XBOX_SERIES;break;default:t->device_type=TITAN_PADEMU_DEVICE_NONE;break;}
    if (t->connected && p->telemetry_packets != 0u) {
        t->buttons=(u16)(~p->ds2.nButtonState);
        t->left_x=p->ds2.LeftStickX;t->left_y=p->ds2.LeftStickY;
        t->right_x=p->ds2.RightStickX;t->right_y=p->ds2.RightStickY;
        t->trigger_l=p->ds2.PressureL2;t->trigger_r=p->ds2.PressureR2;
        t->input_valid=1u;
    } else {
        t->left_x=t->left_y=t->right_x=t->right_y=0x7fu;
    }
    return 1;
}

int ds34usb_get_status(int port)
{
    int ret;

    WaitSema(ds34pad[port].sema);
    ret = ds34pad[port].status;
    SignalSema(ds34pad[port].sema);

    return ret;
}

int ds34usb_get_model(int port)
{
    int ret;

    WaitSema(ds34pad[port].sema);
    if (ds34pad[port].type == GUITAR_GH || ds34pad[port].type == GUITAR_RB) {
        ret = MODEL_GUITAR;
    } else {
        ret = MODEL_PS2;
    }
    SignalSema(ds34pad[port].sema);

    return ret;
}

int ds34usb_init(u8 pads, u8 options)
{
    int pad;

    for (pad = 0; pad < MAX_PADS; pad++) {
        ds34pad[pad].status = 0;
        ds34pad[pad].devId = -1;
        ds34pad[pad].oldled[0] = 0;
        ds34pad[pad].oldled[1] = 0;
        ds34pad[pad].oldled[2] = 0;
        ds34pad[pad].oldled[3] = 0;
        ds34pad[pad].lrum = 0;
        ds34pad[pad].rrum = 0;
        ds34pad[pad].update_rum = 1;
        ds34pad[pad].sema = -1;
        ds34pad[pad].cmd_sema = -1;
        ds34pad[pad].controlEndp = -1;
        ds34pad[pad].interruptEndp = -1;
        ds34pad[pad].outEndp = -1;
        ds34pad[pad].enabled = (pads >> pad) & 1;
        ds34pad[pad].type = 0;

        ds34pad[pad].data[0] = 0xFF;
        ds34pad[pad].data[1] = 0xFF;
        ds34pad[pad].analog_btn = 0;

        mips_memset(&ds34pad[pad].data[2], 0x7F, 4);
        mips_memset(&ds34pad[pad].data[6], 0x00, 12);

        ds34pad[pad].sema = CreateMutex(IOP_MUTEX_UNLOCKED);
        ds34pad[pad].cmd_sema = CreateMutex(IOP_MUTEX_UNLOCKED);

        if (ds34pad[pad].sema < 0 || ds34pad[pad].cmd_sema < 0) {
            DPRINTF("DS34USB: Failed to allocate I/O semaphore.\n");
            return 0;
        }
    }

    if (UsbRegisterDriver(&usb_driver) != USB_RC_OK) {
        DPRINTF("DS34USB: Error registering USB devices\n");
        return 0;
    }

    return 1;
}
