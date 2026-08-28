
/* based on https://github.com/IonAgorria/Arduino-PSRemote */
/* and https://github.com/felis/USB_Host_Shield_2.0 */

#include "types.h"
#include "loadcore.h"
#include "stdio.h"
#include "sifrpc.h"
#include "sysclib.h"
#include "usbd.h"
#include "usbd_macro.h"
#include "thbase.h"
#include "thsemap.h"
#include "ds34bt.h"
#include "sys_utils.h"
#include "padmacro.h"
#include "xbox_protocol.h"
#include <titan_pademu_telemetry.h>
#include "pademu_telemetry_rpc.h"
#include "titan_ble_hogp.h"

//#define DPRINTF(x...) printf(x)
#define DPRINTF(x...)

static int bt_probe(int devId);
static int bt_connect(int devId);
static int bt_disconnect(int devId);
static void bt_config_set(int result, int count, void *arg);

static UsbDriver bt_driver = {NULL, NULL, "ds34bt", bt_probe, bt_connect, bt_disconnect};
static bt_device bt_dev = {-1, -1, -1, -1, -1, -1, DS34BT_STATE_USB_DISCONNECTED};
static TitanPademuBluetoothTransportStatus g_bt_transport;

static void bt_diag_stage(unsigned int stage, int result)
{
    g_bt_transport.stage = stage;
    if (result < 0) g_bt_transport.last_error = result;
    g_bt_transport.usb_dev_id = bt_dev.devId;
    g_bt_transport.usb_status = bt_dev.status;
}

static void bt_diag_endpoints(void)
{
    unsigned int mask = 0;
    if (bt_dev.controlEndp >= 0) mask |= 1u;
    if (bt_dev.interruptEndp >= 0) mask |= 2u;
    if (bt_dev.inEndp >= 0) mask |= 4u;
    if (bt_dev.outEndp >= 0) mask |= 8u;
    g_bt_transport.endpoints_mask = mask;
}

static int chrg_probe(int devId);
static int chrg_connect(int devId);
static int chrg_disconnect(int devId);
static int chrg_dev = -1;
static int chrg_end = -1;

static u16 chrg_pid = 0;

/* P9.2: Sony USB host provisioning.
 * Cache the BT dongle address from HCI Read_BD_ADDR. Pair stays armed
 * across dongle/controller hot-swap when no hub is available. */
static u8 sony_host_bdaddr[6] = {0};
static u8 sony_host_bdaddr_valid = 0;
static u8 sony_pair_requested = 0;
static u8 sony_pair_pending = 0;
static u8 sony_pair_buf[32] __attribute((aligned(4))) = {0};

static void chrg_config_set(int result, int count, void *arg);
static void sony_pair_write_cb(int result, int bytes, void *arg);
static int sony_pair_submit(void);

static UsbDriver chrg_driver = {NULL, NULL, "ds34chrg", chrg_probe, chrg_connect, chrg_disconnect};

static void ds34pad_clear(int pad);
static void ds34pad_init();

static int bt_probe(int devId)
{
    UsbDeviceDescriptor *device = NULL;
    UsbConfigDescriptor *config = NULL;
    UsbInterfaceDescriptor *intf = NULL;

    DPRINTF("DS34BT: probe: devId=%i\n", devId);
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_PROBE, 0, (u32)devId, g_bt_transport.probe_count + 1u, 0u);
    g_bt_transport.probe_count++;
    g_bt_transport.usb_dev_id = devId;
    bt_diag_stage(TITAN_PADEMU_BT_STAGE_PROBE_SEEN, 0);

    if ((bt_dev.devId > 0) && (bt_dev.status & DS34BT_STATE_USB_AUTHORIZED)) {
        DPRINTF("DS34BT: Error - only one device allowed !\n");
        return 0;
    }

    device = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    if (device == NULL) {
        DPRINTF("DS34BT: Error - Couldn't get device descriptor\n");
        bt_diag_stage(TITAN_PADEMU_BT_STAGE_PROBE_REJECTED, -101);
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_PROBE_RESULT, -101, (u32)devId, 0u, 0u);
        return 0;
    }

    pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_DEVICE, 0, ((u32)device->idVendor << 16) | (u32)device->idProduct,
                        (u32)device->bcdDevice, (u32)device->bNumConfigurations);
    if (device->bNumConfigurations < 1) { bt_diag_stage(TITAN_PADEMU_BT_STAGE_PROBE_REJECTED, -102); pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_PROBE_RESULT, -102, (u32)devId, 0u, 0u); return 0; }

    config = (UsbConfigDescriptor *)UsbGetDeviceStaticDescriptor(devId, device, USB_DT_CONFIG);
    if (config == NULL) {
        DPRINTF("DS34BT: Error - Couldn't get configuration descriptor\n");
        bt_diag_stage(TITAN_PADEMU_BT_STAGE_PROBE_REJECTED, -103);
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_PROBE_RESULT, -103, (u32)devId, 0u, 0u);
        return 0;
    }

    if ((config->bNumInterfaces < 1) || (config->wTotalLength < (sizeof(UsbConfigDescriptor) + sizeof(UsbInterfaceDescriptor)))) {
        DPRINTF("DS34BT: Error - No interfaces available\n");
        bt_diag_stage(TITAN_PADEMU_BT_STAGE_PROBE_REJECTED, -104);
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_PROBE_RESULT, -104, (u32)devId, 0u, 0u);
        return 0;
    }

    intf = (UsbInterfaceDescriptor *)((char *)config + config->bLength);

    DPRINTF("DS34BT: bInterfaceClass %X bInterfaceSubClass %X bInterfaceProtocol %X\n", intf->bInterfaceClass, intf->bInterfaceSubClass, intf->bInterfaceProtocol);
    g_bt_transport.usb_identity = ((u32)intf->bInterfaceClass << 24) | ((u32)intf->bInterfaceSubClass << 16) | ((u32)intf->bInterfaceProtocol << 8) | (u32)intf->bNumEndpoints;
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_INTERFACE, 0, g_bt_transport.usb_identity,
                        ((u32)config->bNumInterfaces << 16) | (u32)config->wTotalLength, (u32)config->bConfigurationValue);

    if ((intf->bInterfaceClass != USB_CLASS_WIRELESS_CONTROLLER) ||
        (intf->bInterfaceSubClass != USB_SUBCLASS_RF_CONTROLLER) ||
        (intf->bInterfaceProtocol != USB_PROTOCOL_BLUETOOTH_PROG) ||
        (intf->bNumEndpoints < 3)) {
        bt_diag_stage(TITAN_PADEMU_BT_STAGE_PROBE_REJECTED, -105);
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_PROBE_RESULT, -105, (u32)devId, g_bt_transport.usb_identity, 0u);
        return 0;
    }

    bt_diag_stage(TITAN_PADEMU_BT_STAGE_PROBE_ACCEPTED, 0);
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_PROBE_RESULT, 1, (u32)devId, g_bt_transport.usb_identity, 0u);
    return 1;
}

static int bt_connect(int devId)
{
    unsigned char *cursor;
    unsigned char *config_end;
    UsbDeviceDescriptor *device = NULL;
    UsbConfigDescriptor *config = NULL;
    UsbInterfaceDescriptor *interface = NULL;

    DPRINTF("DS34BT: connect: devId=%i\n", devId);
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_CONNECT, 0, (u32)devId, g_bt_transport.connect_count + 1u, 0u);
    g_bt_transport.connect_count++;
    g_bt_transport.usb_dev_id = devId;
    bt_diag_stage(TITAN_PADEMU_BT_STAGE_CONNECT_SEEN, 0);

    if (bt_dev.devId != -1) {
        DPRINTF("DS34BT: Error - only one device allowed !\n");
        return 1;
    }

    bt_dev.status = DS34BT_STATE_USB_DISCONNECTED;
    bt_dev.controlEndp = -1;
    bt_dev.interruptEndp = -1;
    bt_dev.inEndp = -1;
    bt_dev.outEndp = -1;

    device = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    if (device == NULL || device->bNumConfigurations < 1) {
        DPRINTF("DS34BT: Error - invalid device descriptor\n");
        bt_diag_stage(TITAN_PADEMU_BT_STAGE_CONNECT_FAILED, -201);
        return -1;
    }
    config = (UsbConfigDescriptor *)UsbGetDeviceStaticDescriptor(devId, device, USB_DT_CONFIG);
    if (config == NULL || config->bLength < 2u || config->wTotalLength < (unsigned int)(config->bLength + 2u)) {
        DPRINTF("DS34BT: Error - invalid configuration descriptor\n");
        bt_diag_stage(TITAN_PADEMU_BT_STAGE_CONNECT_FAILED, -202);
        return -1;
    }
    interface = (UsbInterfaceDescriptor *)((unsigned char *)config + config->bLength);
    if ((unsigned char *)interface + 2u > (unsigned char *)config + config->wTotalLength ||
        interface->bLength < 2u || interface->bDescriptorType != USB_DT_INTERFACE) {
        DPRINTF("DS34BT: Error - invalid interface descriptor\n");
        bt_diag_stage(TITAN_PADEMU_BT_STAGE_CONNECT_FAILED, -203);
        return -1;
    }

    bt_dev.controlEndp = UsbOpenEndpoint(devId, NULL);
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_ENDPOINT, bt_dev.controlEndp, 0u, 0u, 0u);
    if (bt_dev.controlEndp < 0) {
        DPRINTF("DS34BT: Error - couldn't open control endpoint\n");
        bt_diag_endpoints();
        bt_diag_stage(TITAN_PADEMU_BT_STAGE_CONNECT_FAILED, bt_dev.controlEndp);
        return -1;
    }

    DPRINTF("DS34BT: Endpoint Count %d \n", interface->bNumEndpoints);
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
        if (endpoint->bmAttributes == USB_ENDPOINT_XFER_BULK) {
            if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT && bt_dev.outEndp < 0) {
                bt_dev.outEndp = UsbOpenEndpointAligned(devId, endpoint);
                pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_ENDPOINT, bt_dev.outEndp,
                                    (u32)endpoint->bEndpointAddress, (u32)endpoint->bmAttributes,
                                    ((u32)endpoint->wMaxPacketSizeHB << 8) | (u32)endpoint->wMaxPacketSizeLB);
                DPRINTF("DS34BT: register Output endpoint id =%i addr=%02X packetSize=%i\n", bt_dev.outEndp, endpoint->bEndpointAddress, (unsigned short int)endpoint->wMaxPacketSizeHB << 8 | endpoint->wMaxPacketSizeLB);
            } else if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN && bt_dev.inEndp < 0) {
                bt_dev.inEndp = UsbOpenEndpointAligned(devId, endpoint);
                pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_ENDPOINT, bt_dev.inEndp,
                                    (u32)endpoint->bEndpointAddress, (u32)endpoint->bmAttributes,
                                    ((u32)endpoint->wMaxPacketSizeHB << 8) | (u32)endpoint->wMaxPacketSizeLB);
                DPRINTF("DS34BT: register Input endpoint id =%i addr=%02X packetSize=%i\n", bt_dev.inEndp, endpoint->bEndpointAddress, (unsigned short int)endpoint->wMaxPacketSizeHB << 8 | endpoint->wMaxPacketSizeLB);
            }
        } else if (endpoint->bmAttributes == USB_ENDPOINT_XFER_INT) {
            if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN && bt_dev.interruptEndp < 0) {
                bt_dev.interruptEndp = UsbOpenEndpoint(devId, endpoint);
                pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_ENDPOINT, bt_dev.interruptEndp,
                                    (u32)endpoint->bEndpointAddress, (u32)endpoint->bmAttributes,
                                    ((u32)endpoint->wMaxPacketSizeHB << 8) | (u32)endpoint->wMaxPacketSizeLB);
                DPRINTF("DS34BT: register Interrupt endpoint id =%i addr=%02X packetSize=%i\n", bt_dev.interruptEndp, endpoint->bEndpointAddress, (unsigned short int)endpoint->wMaxPacketSizeHB << 8 | endpoint->wMaxPacketSizeLB);
            }
        }
        cursor += descriptor_length;
    }

    bt_diag_endpoints();
    if (bt_dev.interruptEndp < 0 || bt_dev.inEndp < 0 || bt_dev.outEndp < 0) {
        DPRINTF("DS34BT: Error - connect failed: not enough endpoints! \n");
        bt_diag_stage(TITAN_PADEMU_BT_STAGE_CONNECT_FAILED, -204);
        if (bt_dev.interruptEndp >= 0) UsbCloseEndpoint(bt_dev.interruptEndp);
        if (bt_dev.inEndp >= 0) UsbCloseEndpoint(bt_dev.inEndp);
        if (bt_dev.outEndp >= 0) UsbCloseEndpoint(bt_dev.outEndp);
        if (bt_dev.controlEndp >= 0) UsbCloseEndpoint(bt_dev.controlEndp);
        bt_dev.controlEndp = -1;
        bt_dev.interruptEndp = -1;
        bt_dev.inEndp = -1;
        bt_dev.outEndp = -1;
        return -1;
    }

    bt_dev.devId = devId;
    bt_dev.status = DS34BT_STATE_USB_AUTHORIZED;
    bt_diag_endpoints();
    bt_diag_stage(TITAN_PADEMU_BT_STAGE_ENDPOINTS_READY, 0);
    {
        int config_rc = UsbSetDeviceConfiguration(bt_dev.controlEndp, config->bConfigurationValue, bt_config_set, NULL);
        g_bt_transport.config_result = config_rc;
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_SET_CONFIG, config_rc, (u32)config->bConfigurationValue,
                            (u32)bt_dev.controlEndp, g_bt_transport.endpoints_mask);
        if (config_rc != USB_RC_OK) {
            bt_diag_stage(TITAN_PADEMU_BT_STAGE_CONFIG_FAILED, config_rc);
            return -1;
        }
    }
    bt_diag_stage(TITAN_PADEMU_BT_STAGE_CONFIG_SUBMITTED, 0);
    return 0;
}

static int bt_disconnect(int devId)
{
    DPRINTF("DS34BT: disconnect: devId=%i\n", devId);
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_DISCONNECT, 0, (u32)devId, bt_dev.status, g_bt_transport.endpoints_mask);

    if (bt_dev.devId == devId && (bt_dev.status & DS34BT_STATE_USB_AUTHORIZED)) {

        if (bt_dev.interruptEndp >= 0)
            UsbCloseEndpoint(bt_dev.interruptEndp);

        if (bt_dev.inEndp >= 0)
            UsbCloseEndpoint(bt_dev.inEndp);

        if (bt_dev.outEndp >= 0)
            UsbCloseEndpoint(bt_dev.outEndp);

        if (bt_dev.controlEndp >= 0)
            UsbCloseEndpoint(bt_dev.controlEndp);

        bt_dev.devId = -1;
        bt_dev.interruptEndp = -1;
        bt_dev.inEndp = -1;
        bt_dev.outEndp = -1;
        bt_dev.controlEndp = -1;
        bt_dev.status = DS34BT_STATE_USB_DISCONNECTED;
        bt_diag_endpoints();
        bt_diag_stage(TITAN_PADEMU_BT_STAGE_DISCONNECTED, 0);

        ds34pad_init();
        SignalSema(bt_dev.hid_sema);
    }

    return 0;
}

int chrg_probe(int devId)
{
    UsbDeviceDescriptor *device = NULL;

    DPRINTF("DS34CHRG: probe: devId=%i\n", devId);

    device = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    if (device == NULL) {
        DPRINTF("DS34CHRG: Error - Couldn't get device descriptor\n");
        return 0;
    }

    if (device->idVendor == DS34_VID && (device->idProduct == DS3_PID || device->idProduct == DS4_PID || device->idProduct == DS4_PID_SLIM || device->idProduct == DUALSENSE_PID || device->idProduct == DUALSENSE_EDGE_PID))
        return 1;

    return 0;
}

int chrg_connect(int devId)
{
    UsbDeviceDescriptor *device = NULL;
    UsbConfigDescriptor *config = NULL;

    DPRINTF("DS34CHRG: connect: devId=%i\n", devId);

    if (chrg_dev != -1) {
        DPRINTF("DS34CHRG: Error - only one device allowed !\n");
        return 1;
    }

    device = (UsbDeviceDescriptor *)UsbGetDeviceStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    if (device == NULL || device->bNumConfigurations < 1)
        return 1;
    config = (UsbConfigDescriptor *)UsbGetDeviceStaticDescriptor(devId, device, USB_DT_CONFIG);
    if (config == NULL)
        return 1;

    chrg_end = UsbOpenEndpoint(devId, NULL);
    if (chrg_end < 0)
        return 1;

    chrg_dev = devId;
    chrg_pid = device->idProduct;
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_PAIR_REQUEST, 0,
                        0x53500002u, (u32)chrg_pid, (u32)sony_pair_requested);
    UsbSetDeviceConfiguration(chrg_end, config->bConfigurationValue, chrg_config_set, NULL);
    return 0;
}

int chrg_disconnect(int devId)
{
    DPRINTF("DS34CHRG: disconnect: devId=%i\n", devId);
    if (chrg_dev == devId && chrg_end >= 0)
        UsbCloseEndpoint(chrg_end);
    chrg_end = -1;
    chrg_dev = -1;
    chrg_pid = 0;
    sony_pair_pending = 0;
    return 0;
}

#define OUTPUT_01_REPORT_SIZE 48
static const u8 hid_cmd_payload_led_arguments[] = {0xff, 0x27, 0x10, 0x00, 0x32};

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
        {{0x00, 0x10, 0x00}, {0x00, 0x7F, 0x00}}, // light green/green/
        {{0x10, 0x10, 0x00}, {0x7F, 0x7F, 0x00}}, // light yellow/yellow
        {{0x00, 0x10, 0x10}, {0x00, 0x7F, 0x7F}}, // light cyan/cyan
};

static u8 link_key[] = // for ds4 authorisation
    {
        0x56, 0xE8, 0x81, 0x38, 0x08, 0x06, 0x51, 0x41,
        0xC0, 0x7F, 0x12, 0xAA, 0xD9, 0x66, 0x3C, 0xCE};


/* P10: Titan Wireless Accessories Manager.
 * The UI owns discovery/pairing UX; PADEMU remains the protocol/HID backend.
 * Nothing here runs at IRX _start(): the RPC first invokes the existing
 * boot-safe pademu_transport_init_on_demand() boundary. */
#define TITAN_CLASSIC_BOND_MAGIC TITAN_PADEMU_CLASSIC_BOND_MAGIC
#define TITAN_CLASSIC_BOND_VERSION TITAN_PADEMU_CLASSIC_BOND_VERSION
#define TITAN_WIRELESS_INVALID_INDEX 0xffffffffu
#define TITAN_WIRELESS_BA(n) (0x42410000u | ((u32)(n) & 0xffu))

typedef TitanPademuClassicBondEntry TitanClassicBondEntry;
typedef TitanPademuClassicBondFile TitanClassicBondFile;

typedef char TitanXboxBondWireLayoutMatches[(sizeof(TitanPademuXboxBond) == sizeof(TitanBleBond)) ? 1 : -1];

static TitanPademuWirelessState g_wireless;
static TitanClassicBondFile g_classic_bonds;
static int g_classic_bonds_loaded = 0;
static int g_classic_bonds_dirty = 0;
static int g_xbox_bond_dirty = 0;
static u32 g_bond_generation = 1u;
static int g_wireless_direct_pad = -1;
static int g_wireless_name_index = -1;
static int g_wireless_scan_pending = 0; /* 0 idle, 1 event-mask pending, 2 SSP-mode pending */
static int g_wireless_scan_requested = 0;
/* P10.4: RPC can receive BEGIN_SCAN before PAD_INIT/HCI are ready.  Keep a
 * bootstrap hint outside g_wireless lifetime so Read_BD_ADDR cannot race the
 * deferred RPC pump and incorrectly start the optional Xbox BLE lane first. */
static volatile int g_wireless_bootstrap_scan_pending = 0;
static int g_wireless_hci_startup_ready = 0;
static int g_wireless_ssp_event_mask = 0;
static int g_wireless_ssp_mode = 0;
/* P10.1_ACCESSORY_TRUTH_SCAN_RETRY: 1=Classic scan waits for LE scan disable,
 * 2=Classic connect waits for LE scan disable. */
static int g_wireless_wait_ble_stop = 0;
/* P10.5.4: once Classic SSP/authentication succeeds, the only remaining
 * handoff before HID is the first L2CAP Control connection request.  Track
 * that tiny window explicitly so a lost/busy USB bulk submit can be retried
 * once without ever creating a second ACL connection. */
static int g_wireless_l2cap_watch_pad = -1;
static unsigned int g_wireless_l2cap_watch_queries = 0u;
static unsigned int g_wireless_l2cap_retry_count = 0u;

static int wireless_addr_equal(const u8 *a, const u8 *b)
{
    return a && b && memcmp(a, b, 6) == 0;
}

static void wireless_copy_name(char *dst, unsigned cap, const char *src)
{
    unsigned i = 0;
    if (!dst || !cap) return;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* P10.5.5: Sony Bluetooth names are firmware-dependent.  Older DualSense
 * firmware commonly reports "Wireless Controller"; newer firmware may report
 * "DualSense Wireless Controller".  Keep generation-specific HID probing
 * deferred, but classify all known Sony controller names onto the DS4-compatible
 * Classic L2CAP channel layout.  No libc strcmp dependency is introduced here. */
static int wireless_name_equals(const char *a, const char *b)
{
    unsigned i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int wireless_is_sony_controller_name(const char *name)
{
    return wireless_name_equals(name, "Wireless Controller") ||
           wireless_name_equals(name, "DualSense Wireless Controller") ||
           wireless_name_equals(name, "DualSense Edge Wireless Controller") ||
           wireless_name_equals(name, "DUALSHOCK 4 Wireless Controller");
}

static int wireless_bond_find(const u8 address[6])
{
    unsigned i;
    for (i = 0; i < g_classic_bonds.count && i < TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES; i++)
        if (wireless_addr_equal(g_classic_bonds.entries[i].address, address)) return (int)i;
    return -1;
}

static int wireless_accessory_find(const u8 address[6])
{
    unsigned i;
    for (i = 0; i < g_wireless.count && i < TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES; i++)
        if (wireless_addr_equal(g_wireless.accessories[i].address, address)) return (int)i;
    return -1;
}

static int wireless_accessory_get_or_add(const u8 address[6])
{
    int found = wireless_accessory_find(address);
    TitanPademuWirelessAccessory *a;
    if (found >= 0) return found;
    if (g_wireless.count >= TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES) return -1;
    found = (int)g_wireless.count++;
    a = &g_wireless.accessories[found];
    mips_memset(a, 0, sizeof(*a));
    mips_memcpy(a->address, address, 6);
    return found;
}

static void wireless_state_changed(void)
{
    g_wireless.generation++;
    if (!g_wireless.generation) g_wireless.generation = 1;
    g_wireless.last_hci_event = g_bt_transport.last_hci_event;
    g_wireless.last_hci_opcode = g_bt_transport.last_hci_opcode;
}

static void wireless_publish_bonds(void)
{
    unsigned i;
    for (i = 0; i < g_classic_bonds.count && i < TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES; i++) {
        int wi = wireless_accessory_get_or_add(g_classic_bonds.entries[i].address);
        if (wi >= 0) {
            TitanPademuWirelessAccessory *a = &g_wireless.accessories[wi];
            a->flags |= TITAN_PADEMU_WIRELESS_PAIRED | TITAN_PADEMU_WIRELESS_CONTROLLER;
            a->device_type = g_classic_bonds.entries[i].device_type;
            wireless_copy_name(a->name, sizeof(a->name), g_classic_bonds.entries[i].name);
        }
    }
}

static void wireless_load_bonds(void)
{
    if (g_classic_bonds_loaded) return;
    g_classic_bonds_loaded = 1;
    if (g_classic_bonds.magic != TITAN_CLASSIC_BOND_MAGIC ||
        g_classic_bonds.version != TITAN_CLASSIC_BOND_VERSION ||
        g_classic_bonds.count > TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES) {
        mips_memset(&g_classic_bonds, 0, sizeof(g_classic_bonds));
        g_classic_bonds.magic = TITAN_CLASSIC_BOND_MAGIC;
        g_classic_bonds.version = TITAN_CLASSIC_BOND_VERSION;
    }
    wireless_publish_bonds();
}

/* P10.5: no mass:/ I/O is allowed from PADEMU. Bond persistence belongs to
 * the EE storage worker and is synchronized over the dedicated BT RPC. */
static void wireless_flush_bonds(void)
{
    /* Compatibility call site: persistence is asynchronous and EE-owned. */
}

static void wireless_store_key(const u8 address[6], const u8 key[16])
{
    int bi = wireless_bond_find(address);
    int wi;
    TitanClassicBondEntry *b;
    if (bi < 0) {
        if (g_classic_bonds.count >= TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES) {
            /* Prefer replacing the oldest slot over silently losing a successful bond. */
            bi = 0;
        } else bi = (int)g_classic_bonds.count++;
    }
    b = &g_classic_bonds.entries[bi];
    mips_memset(b, 0, sizeof(*b));
    mips_memcpy(b->address, address, 6);
    mips_memcpy(b->key, key, 16);
    wi = wireless_accessory_find(address);
    if (wi >= 0) {
        b->device_type = g_wireless.accessories[wi].device_type;
        wireless_copy_name(b->name, sizeof(b->name), g_wireless.accessories[wi].name);
        g_wireless.accessories[wi].flags |= TITAN_PADEMU_WIRELESS_PAIRED;
    }
    g_classic_bonds_dirty = 1;
    g_bond_generation++; if (!g_bond_generation) g_bond_generation = 1u;
    wireless_state_changed();
}

static void wireless_remove_bond(const u8 address[6])
{
    int bi = wireless_bond_find(address);
    unsigned i;
    if (bi < 0) return;
    for (i = (unsigned)bi; i + 1u < g_classic_bonds.count; i++)
        mips_memcpy(&g_classic_bonds.entries[i], &g_classic_bonds.entries[i + 1u], sizeof(g_classic_bonds.entries[i]));
    if (g_classic_bonds.count) g_classic_bonds.count--;
    mips_memset(&g_classic_bonds.entries[g_classic_bonds.count], 0, sizeof(g_classic_bonds.entries[0]));
    g_classic_bonds_dirty = 1;
    g_bond_generation++; if (!g_bond_generation) g_bond_generation = 1u;
}

static void wireless_update_bond_metadata(const u8 address[6], u8 device_type, const char *name)
{
    int bi = wireless_bond_find(address);
    int changed = 0;
    if (bi < 0) return;
    if (g_classic_bonds.entries[bi].device_type != device_type) {
        g_classic_bonds.entries[bi].device_type = device_type;
        changed = 1;
    }
    if (name && name[0] && memcmp(g_classic_bonds.entries[bi].name, name, sizeof(g_classic_bonds.entries[bi].name)) != 0) {
        wireless_copy_name(g_classic_bonds.entries[bi].name, sizeof(g_classic_bonds.entries[bi].name), name);
        changed = 1;
    }
    if (changed) {
        g_classic_bonds_dirty = 1;
        g_bond_generation++; if (!g_bond_generation) g_bond_generation = 1u;
    }
}

#define TITAN_SONY_HID_REQ_OUT (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE)
#define TITAN_HID_REPORT_TYPE_FEATURE 0x03u
#define TITAN_DUALSENSE_PAIR_REPORT_ID 0x0A
#define TITAN_DUALSENSE_PAIR_REPORT_SIZE 27

static void sony_pair_write_cb(int result, int bytes, void *arg)
{
    (void)bytes;
    (void)arg;
    sony_pair_pending = 0;
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_PAIR_REQUEST, result,
                        0x53500005u, (u32)chrg_pid, (u32)sony_pair_requested);
    if (result == USB_RC_OK)
        sony_pair_requested = 0;
}

static int sony_pair_submit(void)
{
    int ret;
    if (!sony_pair_requested || sony_pair_pending ||
        !sony_host_bdaddr_valid || chrg_end < 0)
        return 0;
    if (chrg_pid != DUALSENSE_PID && chrg_pid != DUALSENSE_EDGE_PID)
        return 0;

    mips_memset(sony_pair_buf, 0, TITAN_DUALSENSE_PAIR_REPORT_SIZE);
    sony_pair_buf[0] = TITAN_DUALSENSE_PAIR_REPORT_ID;
    mips_memcpy(sony_pair_buf + 1, sony_host_bdaddr, 6);
    mips_memcpy(sony_pair_buf + 7, link_key, sizeof(link_key));

    sony_pair_pending = 1;
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_PAIR_REQUEST, 0,
                        0x53500004u,
                        (u32)sony_host_bdaddr[0] |
                        ((u32)sony_host_bdaddr[1] << 8) |
                        ((u32)sony_host_bdaddr[2] << 16) |
                        ((u32)sony_host_bdaddr[3] << 24),
                        (u32)sony_host_bdaddr[4] |
                        ((u32)sony_host_bdaddr[5] << 8) |
                        ((u32)chrg_pid << 16));

    ret = UsbControlTransfer(chrg_end,
                             TITAN_SONY_HID_REQ_OUT,
                             USB_REQ_SET_REPORT,
                             (TITAN_HID_REPORT_TYPE_FEATURE << 8) |
                                 TITAN_DUALSENSE_PAIR_REPORT_ID,
                             0,
                             TITAN_DUALSENSE_PAIR_REPORT_SIZE,
                             sony_pair_buf,
                             sony_pair_write_cb,
                             NULL);
    if (ret != USB_RC_OK) {
        sony_pair_pending = 0;
        return 0;
    }
    return 1;
}

static void chrg_config_set(int result, int count, void *arg)
{
    (void)count;
    (void)arg;
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_PAIR_REQUEST, result,
                        0x53500003u, (u32)chrg_pid,
                        (u32)sony_host_bdaddr_valid);
    if (result != USB_RC_OK) {
        sony_pair_pending = 0;
        return;
    }
    (void)sony_pair_submit();
}


// Taken from nefarius' SCPToolkit
// https://github.com/nefarius/ScpToolkit/blob/master/ScpControl/ScpControl.ini
// Valid MAC addresses used by Sony
static u8 GenuineMacAddress[][3] =
    {
        // Bluetooth chips by ALPS ELECTRIC CO., LTD
        {0x00, 0x02, 0xC7},
        {0x00, 0x06, 0xF5},
        {0x00, 0x06, 0xF7},
        {0x00, 0x07, 0x04},
        {0x00, 0x16, 0xFE},
        {0x00, 0x19, 0xC1},
        {0x00, 0x1B, 0xFB},
        {0x00, 0x1E, 0x3D},
        {0x00, 0x21, 0x4F},
        {0x00, 0x23, 0x06},
        {0x00, 0x24, 0x33},
        {0x00, 0x26, 0x43},
        {0x00, 0xA0, 0x79},
        {0x04, 0x76, 0x6E},
        {0x04, 0x98, 0xF3},
        {0x28, 0xA1, 0x83},
        {0x34, 0xC7, 0x31},
        {0x38, 0xC0, 0x96},
        {0x60, 0x38, 0x0E},
        {0x64, 0xD4, 0xBD},
        {0xAC, 0x7A, 0x4D},
        {0xE0, 0x75, 0x0A},
        {0xE0, 0xAE, 0x5E},
        {0xFC, 0x62, 0xB9},
        // Bluetooth chips by AzureWave Technology Inc.
        {0xE0, 0xB9, 0xA5},
        {0xDC, 0x85, 0xDE},
        {0xD0, 0xE7, 0x82},
        {0xB0, 0xEE, 0x45},
        {0xAC, 0x89, 0x95},
        {0xA8, 0x1D, 0x16},
        {0x94, 0xDB, 0xC9},
        {0x80, 0xD2, 0x1D},
        {0x80, 0xA5, 0x89},
        {0x78, 0x18, 0x81},
        {0x74, 0xF0, 0x6D},
        {0x74, 0xC6, 0x3B},
        {0x74, 0x2F, 0x68},
        {0x6C, 0xAD, 0xF8},
        {0x6C, 0x71, 0xD9},
        {0x60, 0x5B, 0xB4},
        {0x5C, 0x96, 0x56},
        {0x54, 0x27, 0x1E},
        {0x4C, 0xAA, 0x16},
        {0x48, 0x5D, 0x60},
        {0x44, 0xD8, 0x32},
        {0x40, 0xE2, 0x30},
        {0x38, 0x4F, 0xF0},
        {0x28, 0xC2, 0xDD},
        {0x24, 0x0A, 0x64},
        {0x1C, 0x4B, 0xD6},
        {0x08, 0xA9, 0x5A},
        {0x00, 0x25, 0xD3},
        {0x00, 0x24, 0x23},
        {0x00, 0x22, 0x43},
        {0x00, 0x15, 0xAF},
        // fake with AirohaTechnologyCorp's Chip
        {0x0C, 0xFC, 0x83}};

#define REQ_HCI_OUT     (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_DEVICE)
#define HCI_COMMAND_REQ 0

#define MAX_PADS  4
#define MAX_DELAY 10

static u8 hci_buf[MAX_BUFFER_SIZE] __attribute((aligned(4))) = {0};
static u8 l2cap_buf[MAX_BUFFER_SIZE + 32] __attribute((aligned(4))) = {0};

/* P10.6.4: runtime Bulk-IN L2CAP RX rearm reliability.
 * Real-hardware P10.6.3 traces showed HID/L2CAP traffic stopping cleanly
 * before HCI Disconnection Complete. Historically the callback ignored the
 * return code of the next UsbBulkTransfer(), so one transient submit failure
 * could permanently orphan the controller RX chain. */
#define P1064_L2CAP_REARM_ATTEMPTS 4u
#define P1064_L2CAP_REARM_DELAY_US 500u
static volatile u8 g_p1064_l2cap_rearm_pending = 0u;
static u32 g_p1064_l2cap_rearm_failures = 0u;
static u32 g_p1064_l2cap_rearm_recoveries = 0u;

/* P10.6.5: discriminate callback completion failure from radio/link timeout. */
static u32 g_p1065_l2cap_cb_count = 0u;
static int g_p1065_l2cap_last_result = 0;
static int g_p1065_l2cap_last_bytes = 0;

static u8 hci_cmd_buf[MAX_BUFFER_SIZE] __attribute((aligned(4))) = {0};
static int g_hci_event_bytes = 0;
static u8 l2cap_cmd_buf[MAX_BUFFER_SIZE + 32] __attribute((aligned(4))) = {0};

static u8 identifier = 0;
/* P10.5.6: Sony basic Bluetooth HID + bounded post-connect RX tracing. */
static u8 g_p1056_sony_feature05_requested[MAX_PADS] = {0};
static u32 g_p1056_l2cap_hid_rx_packets = 0;
/* P10.5.7: HCI ACL -> complete L2CAP PDU reassembly before HID parsing. */
#define P1057_ACL_REASSEMBLY_CAP 256
static u8 g_p1057_acl_reassembly[P1057_ACL_REASSEMBLY_CAP];
static u16 g_p1057_acl_reassembly_used = 0;
static u16 g_p1057_acl_reassembly_expected = 0;
static u16 g_p1057_acl_reassembly_handle = 0;
static u8 g_p1057_acl_reassembly_active = 0;
static u32 g_p1057_acl_reassembled_packets = 0;
static u32 g_p1057_acl_reassembly_errors = 0;
static u8 g_press_emu = 0;
static u8 enable_fake = 0;

static int HCI_Command(int nbytes, u8 *dataptr);
static void ds34pad_clear(int pad);
static ds34bt_pad_t ds34pad[MAX_PADS];
static void telemetry_bt_ds3(ds34bt_pad_t *pad,u8 raw)
{ static const u8 pct[6]={0,1,25,50,75,100}; pad->battery_raw=raw; if(raw==0xEE){pad->battery_state=TITAN_PADEMU_BATTERY_CHARGING;pad->battery_valid=0;pad->battery_percent=0;}else if(raw==0xEF){pad->battery_state=TITAN_PADEMU_BATTERY_FULL;pad->battery_valid=1;pad->battery_percent=100;}else if(raw<=5){pad->battery_state=TITAN_PADEMU_BATTERY_LEVEL;pad->battery_valid=1;pad->battery_percent=pct[raw];}else{pad->battery_state=TITAN_PADEMU_BATTERY_UNKNOWN;pad->battery_valid=0;pad->battery_percent=0;} }
static void telemetry_bt_ds4(ds34bt_pad_t *pad,const struct ds4report *r)
{ u8 cap=r->Power;pad->battery_raw=(u8)((r->Usb_plugged?0x10:0)|cap);if(cap==0x0B){pad->battery_state=TITAN_PADEMU_BATTERY_FULL;pad->battery_percent=100;pad->battery_valid=1;}else if(cap<=10){pad->battery_state=r->Usb_plugged?TITAN_PADEMU_BATTERY_CHARGING:TITAN_PADEMU_BATTERY_LEVEL;pad->battery_percent=(u8)(cap*10);pad->battery_valid=1;}else{pad->battery_state=TITAN_PADEMU_BATTERY_UNKNOWN;pad->battery_percent=0;pad->battery_valid=0;} }
static void telemetry_bt_dualsense(ds34bt_pad_t *pad,u8 status0)
{ u8 cap=(u8)(status0&15),st=(u8)(status0>>4);pad->battery_raw=status0;if(cap>10){pad->battery_state=TITAN_PADEMU_BATTERY_UNKNOWN;pad->battery_valid=0;pad->battery_percent=0;return;}pad->battery_percent=(u8)(cap*10);pad->battery_valid=1;if(st==1)pad->battery_state=TITAN_PADEMU_BATTERY_CHARGING;else if(st==2){pad->battery_state=TITAN_PADEMU_BATTERY_FULL;pad->battery_percent=100;}else pad->battery_state=TITAN_PADEMU_BATTERY_LEVEL; }
static TitanBleHogp xbox_ble;
static TitanBleBond xbox_ble_bond;
static int xbox_ble_pad = -1;
static u8 xbox_ble_random_pool[32];
static u8 xbox_ble_random_count = 0;
/* BLE capability is independent from Classic BR/EDR discovery.
 *  0 = unknown/not probed yet
 *  1 = LE HCI primitives confirmed
 * -1 = LE HCI command rejected/unsupported
 */
static int g_xbox_ble_hci_capable = 0;
static int g_xbox_ble_resume_pending = 0;

static int xbox_ble_send_hci(void *user, const u8 *cmd, unsigned n)
{
    (void)user;
    return HCI_Command((int)n, (u8 *)cmd);
}
static int xbox_ble_send_acl(void *user, u16 handle, u16 cid, const u8 *data, unsigned length)
{
    (void)user;
    if (length > MAX_BUFFER_SIZE) return -1;
    l2cap_cmd_buf[0]=(u8)(handle&0xff); l2cap_cmd_buf[1]=(u8)(((handle>>8)&0x0f)|0x20);
    l2cap_cmd_buf[2]=(u8)((4+length)&0xff); l2cap_cmd_buf[3]=(u8)((4+length)>>8);
    l2cap_cmd_buf[4]=(u8)(length&0xff); l2cap_cmd_buf[5]=(u8)(length>>8);
    l2cap_cmd_buf[6]=(u8)(cid&0xff); l2cap_cmd_buf[7]=(u8)(cid>>8);
    if (length) mips_memcpy(l2cap_cmd_buf+8,data,length);
    return UsbBulkTransfer(bt_dev.outEndp,l2cap_cmd_buf,8+(int)length,NULL,NULL);
}
static int xbox_ble_random(void *user, u8 *out, unsigned n)
{
    (void)user;
    if (n > xbox_ble_random_count) return 0;
    mips_memcpy(out,xbox_ble_random_pool,n);
    xbox_ble_random_count=(u8)(xbox_ble_random_count-n);
    if (xbox_ble_random_count) mips_memcpy(xbox_ble_random_pool,xbox_ble_random_pool+n,xbox_ble_random_count);
    return 1;
}
static int xbox_ble_load_bond(void *user, TitanBleBond *bond)
{
    (void)user;
    if (!bond || xbox_ble_bond.magic != TITAN_BLE_BOND_VALID) return 0;
    *bond = xbox_ble_bond;
    return 1;
}

static int xbox_ble_store_bond(void *user, const TitanBleBond *bond)
{
    (void)user;
    if (!bond || bond->magic != TITAN_BLE_BOND_VALID) return 0;
    xbox_ble_bond = *bond;
    g_xbox_bond_dirty = 1;
    g_bond_generation++; if (!g_bond_generation) g_bond_generation = 1u;
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0, TITAN_WIRELESS_BA(14),
                        g_bond_generation, 1u);
    return 1;
}
static int xbox_ble_alloc_pad(void)
{
    int i; if (xbox_ble_pad>=0 && xbox_ble_pad<MAX_PADS) return xbox_ble_pad;
    for(i=0;i<MAX_PADS;i++) if(ds34pad[i].enabled && !pad_status_check(DS34BT_STATE_RUNNING,i)){xbox_ble_pad=i;return i;}
    return -1;
}
static void xbox_ble_state(void *user, TitanBleState state)
{
    int pad; (void)user;
    if(state==TITAN_BLE_CONNECTED||state==TITAN_BLE_READY){pad=xbox_ble_alloc_pad();if(pad>=0){ds34pad[pad].hci_handle=xbox_ble.handle;ds34pad[pad].type=XBOX_SERIES;ds34pad[pad].isfake=0;pad_status_set(DS34BT_STATE_CONNECTED,pad);if(state==TITAN_BLE_READY)pad_status_set(DS34BT_STATE_RUNNING,pad);}}
    else if(state==TITAN_BLE_FAILED && xbox_ble_pad>=0){ds34pad_clear(xbox_ble_pad);xbox_ble_pad=-1;}
}
static void xbox_ble_input(void *user, const u8 *report, unsigned n)
{
    int guide = 0, pad = xbox_ble_alloc_pad();

    (void)user;
    if (pad < 0)
        return;

    if (titan_xbox_bt_hid_parse_ds2(report, (int)n, &ds34pad[pad].ds2, &guide)) {
        ds34pad[pad].type = XBOX_SERIES;
        ds34pad[pad].system_button = guide ? 1 : 0;
        pademu_telemetry_system_button((unsigned)pad, ds34pad[pad].type, ds34pad[pad].system_button);
        ds34pad[pad].battery_state = TITAN_PADEMU_BATTERY_UNKNOWN;
        ds34pad[pad].battery_valid = 0;
        ds34pad[pad].telemetry_packets++;
        if (ds34pad[pad].telemetry_packets <= 4u || (ds34pad[pad].telemetry_packets & 63u) == 0u)
            pademu_trace_record(TITAN_PADEMU_TRACE_BT_HID_INPUT,0,(u32)pad,(u32)ds34pad[pad].type,ds34pad[pad].telemetry_packets);
        pad_status_set(DS34BT_STATE_RUNNING, pad);
    }
}
static void xbox_ble_request_rand(void)
{
    u8 cmd[3]={0x18,0x20,0x00}; if(xbox_ble_random_count<=16) HCI_Command(3,cmd);
}
static void xbox_ble_init_transport(void)
{
    TitanBleIo io; mips_memset(&io,0,sizeof(io)); io.send_hci=xbox_ble_send_hci;io.send_acl=xbox_ble_send_acl;io.random=xbox_ble_random;io.load_bond=xbox_ble_load_bond;io.store_bond=xbox_ble_store_bond;io.input_report=xbox_ble_input;io.state_changed=xbox_ble_state;
    TitanBleHogp_Init(&xbox_ble,&io);
    xbox_ble_pad=-1;
    xbox_ble_random_count=0;
    g_xbox_ble_hci_capable=0;
    g_xbox_ble_resume_pending=0;
}


static void hci_event_cb(int resultCode, int bytes, void *arg);
static void l2cap_event_cb(int resultCode, int bytes, void *arg);

/* Must be called while bt_dev.hid_sema is held. origin:
 * 0=L2CAP callback, 1=HCI callback, 2=get_data, 3=get_status, 4=telemetry. */
static int p1064_l2cap_rx_arm_locked(void *arg, u32 origin)
{
    unsigned int attempt;
    int rc = -1;
    int was_pending = g_p1064_l2cap_rearm_pending ? 1 : 0;

    if (bt_dev.inEndp < 0 ||
        !(bt_dev.status & DS34BT_STATE_USB_CONFIGURED)) {
        g_p1064_l2cap_rearm_pending = 1u;
        return -1;
    }

    for (attempt = 0u; attempt < P1064_L2CAP_REARM_ATTEMPTS; ++attempt) {
        rc = UsbBulkTransfer(bt_dev.inEndp, l2cap_buf, MAX_BUFFER_SIZE + 23,
                             l2cap_event_cb, arg);
        if (rc == USB_RC_OK) {
            if (was_pending || attempt != 0u) {
                g_p1064_l2cap_rearm_recoveries++;
                pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_RX_ARM, 0,
                                    0x50313634u, /* P164 */
                                    (origin << 16) | (attempt + 1u),
                                    g_p1064_l2cap_rearm_recoveries);
            }
            g_p1064_l2cap_rearm_pending = 0u;
            return rc;
        }

        g_p1064_l2cap_rearm_pending = 1u;
        g_p1064_l2cap_rearm_failures++;
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_RX_ARM, rc,
                            0x50313634u, /* P164 */
                            (origin << 16) | (attempt + 1u),
                            g_p1064_l2cap_rearm_failures);
        if (attempt + 1u < P1064_L2CAP_REARM_ATTEMPTS)
            DelayThread(P1064_L2CAP_REARM_DELAY_US);
    }
    return rc;
}

static void p1064_l2cap_rx_recover_if_needed_locked(u32 origin)
{
    if (g_p1064_l2cap_rearm_pending &&
        bt_dev.inEndp >= 0 &&
        (bt_dev.status & DS34BT_STATE_USB_CONFIGURED))
        (void)p1064_l2cap_rx_arm_locked(NULL, origin);
}

static void p1064_l2cap_rx_recover_if_needed(u32 origin)
{
    if (!g_p1064_l2cap_rearm_pending || bt_dev.hid_sema < 0)
        return;
    WaitSema(bt_dev.hid_sema);
    p1064_l2cap_rx_recover_if_needed_locked(origin);
    SignalSema(bt_dev.hid_sema);
}


static int hid_initDS34(int pad);
static int HID_command(u16 handle, u16 scid, u8 *data, u8 length, int pad);
static int hid_LEDRumbleCommand(u8 *led, u8 lrum, u8 rrum, int pad);
static void hid_readReport(u8 *data, int bytes, int pad);

static int l2cap_connection_request(u16 handle, u8 rxid, u16 scid, u16 psm);
static int hci_reset();

static void bt_config_set(int result, int count, void *arg)
{
    int hci_event_rc, l2cap_rc, reset_rc;
    (void)count; (void)arg;
    g_bt_transport.config_result = result;
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_CONFIG_CB, result, (u32)count, bt_dev.status, g_bt_transport.endpoints_mask);
    if (result != USB_RC_OK) {
        bt_diag_stage(TITAN_PADEMU_BT_STAGE_CONFIG_FAILED, result);
        return;
    }

    bt_diag_stage(TITAN_PADEMU_BT_STAGE_CONFIGURED, 0);
    PollSema(bt_dev.hid_sema);

    hci_event_rc = UsbInterruptTransfer(bt_dev.interruptEndp, hci_buf, MAX_BUFFER_SIZE, hci_event_cb, NULL);
    l2cap_rc = UsbBulkTransfer(bt_dev.inEndp, l2cap_buf, MAX_BUFFER_SIZE, l2cap_event_cb, NULL);
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_RX_ARM, hci_event_rc, (u32)l2cap_rc,
                        (u32)bt_dev.interruptEndp, (u32)bt_dev.inEndp);
    if (hci_event_rc != USB_RC_OK || l2cap_rc != USB_RC_OK) {
        bt_diag_stage(TITAN_PADEMU_BT_STAGE_CONNECT_FAILED, hci_event_rc != USB_RC_OK ? hci_event_rc : l2cap_rc);
        SignalSema(bt_dev.hid_sema);
        return;
    }
    g_p1064_l2cap_rearm_pending = 0u;

    ds34pad_init();
    g_wireless_hci_startup_ready = 0;
    reset_rc = hci_reset();
    g_bt_transport.reset_submit_result = reset_rc;
    if (reset_rc != USB_RC_OK) bt_diag_stage(TITAN_PADEMU_BT_STAGE_HCI_RESET_FAILED, reset_rc);
    else bt_diag_stage(TITAN_PADEMU_BT_STAGE_HCI_RESET_SUBMITTED, 0);

    SignalSema(bt_dev.hid_sema);
}

/* P10.5.6: DualSense Bluetooth defaults to the compact report 0x01.
 * A GET_REPORT(FEATURE, 0x05) promotes it to the full 0x31 stream.  Do this
 * asynchronously on the existing HID control CID, exactly once per connection. */
static int hid_request_sony_bt_feature05(int pad)
{
    u8 request[2];
    if (pad < 0 || pad >= MAX_PADS)
        return -1;
    request[0] = HID_THDR_GET_REPORT_FEATURE;
    request[1] = 0x05;
    return HID_command(ds34pad[pad].hci_handle,
                       ds34pad[pad].control_scid,
                       request, sizeof(request), pad);
}

/************************************************************/
/* HCI Commands                                             */
/************************************************************/

static int HCI_Command(int nbytes, u8 *dataptr)
{
    int rc = UsbControlTransfer(bt_dev.controlEndp, REQ_HCI_OUT, HCI_COMMAND_REQ, 0, 0, nbytes, dataptr, NULL, NULL);
    u32 opcode = (dataptr && nbytes >= 2) ? ((u32)dataptr[0] | ((u32)dataptr[1] << 8)) : 0u;
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_HCI_COMMAND, rc, opcode, (u32)nbytes, (u32)bt_dev.controlEndp);
    return rc;
}

static int hci_reset()
{
    hci_cmd_buf[0] = HCI_OCF_RESET;
    hci_cmd_buf[1] = HCI_OGF_CTRL_BBAND;
    hci_cmd_buf[2] = 0x00; // Parameter Total Length = 0

    return HCI_Command(3, hci_cmd_buf);
}

static int hci_read_bdaddr(void)
{
    hci_cmd_buf[0]=0x09; hci_cmd_buf[1]=(0x04<<2); hci_cmd_buf[2]=0; return HCI_Command(3,hci_cmd_buf);
}

static int hci_write_scan_enable(u8 conf)
{
    hci_cmd_buf[0] = HCI_OCF_WRITE_SCAN_ENABLE;
    hci_cmd_buf[1] = HCI_OGF_CTRL_BBAND;
    hci_cmd_buf[2] = 0x01;
    hci_cmd_buf[3] = conf;

    return HCI_Command(4, hci_cmd_buf);
}

static int hci_accept_connection(u8 *bdaddr)
{
    hci_cmd_buf[0] = HCI_OCF_ACCEPT_CONNECTION; // HCI OCF = 9
    hci_cmd_buf[1] = HCI_OGF_LINK_CNTRL;        // HCI OGF = 1
    hci_cmd_buf[2] = 0x07;                      // parameter length 7
    hci_cmd_buf[3] = *bdaddr;                   // 6 octet bluetooth address
    hci_cmd_buf[4] = *(bdaddr + 1);
    hci_cmd_buf[5] = *(bdaddr + 2);
    hci_cmd_buf[6] = *(bdaddr + 3);
    hci_cmd_buf[7] = *(bdaddr + 4);
    hci_cmd_buf[8] = *(bdaddr + 5);
    hci_cmd_buf[9] = 0x01; // switch role to (slave = 1 / master = 0)

    return HCI_Command(10, hci_cmd_buf);
}

static int hci_remote_name(u8 *bdaddr)
{
    hci_cmd_buf[0] = HCI_OCF_REMOTE_NAME; // HCI OCF = 19
    hci_cmd_buf[1] = HCI_OGF_LINK_CNTRL;  // HCI OGF = 1
    hci_cmd_buf[2] = 0x0A;                // parameter length = 10
    hci_cmd_buf[3] = *bdaddr;             // 6 octet bluetooth address
    hci_cmd_buf[4] = *(bdaddr + 1);
    hci_cmd_buf[5] = *(bdaddr + 2);
    hci_cmd_buf[6] = *(bdaddr + 3);
    hci_cmd_buf[7] = *(bdaddr + 4);
    hci_cmd_buf[8] = *(bdaddr + 5);
    hci_cmd_buf[9] = 0x01;  // Page Scan Repetition Mode
    hci_cmd_buf[10] = 0x00; // Reserved
    hci_cmd_buf[11] = 0x00; // Clock offset - low byte
    hci_cmd_buf[12] = 0x00; // Clock offset - high byte

    return HCI_Command(13, hci_cmd_buf);
}

static int hci_reject_connection(u8 *bdaddr)
{
    hci_cmd_buf[0] = HCI_OCF_REJECT_CONNECTION; // HCI OCF = A
    hci_cmd_buf[1] = HCI_OGF_LINK_CNTRL;        // HCI OGF = 1
    hci_cmd_buf[2] = 0x07;                      // parameter length 7
    hci_cmd_buf[3] = *bdaddr;                   // 6 octet bluetooth address
    hci_cmd_buf[4] = *(bdaddr + 1);
    hci_cmd_buf[5] = *(bdaddr + 2);
    hci_cmd_buf[6] = *(bdaddr + 3);
    hci_cmd_buf[7] = *(bdaddr + 4);
    hci_cmd_buf[8] = *(bdaddr + 5);
    hci_cmd_buf[9] = 0x09; // reason max connection

    return HCI_Command(10, hci_cmd_buf);
}

static int hci_disconnect(u16 handle)
{
    hci_cmd_buf[0] = HCI_OCF_DISCONNECT;         // HCI OCF = 6
    hci_cmd_buf[1] = HCI_OGF_LINK_CNTRL;         // HCI OGF = 1
    hci_cmd_buf[2] = 0x03;                       // parameter length = 3
    hci_cmd_buf[3] = (u8)(handle & 0xFF);        // connection handle - low byte
    hci_cmd_buf[4] = (u8)((handle >> 8) & 0x0F); // connection handle - high byte
    hci_cmd_buf[5] = 0x13;                       // reason

    return HCI_Command(6, hci_cmd_buf);
}

static int hci_change_connection_type(u16 handle)
{
    hci_cmd_buf[0] = HCI_OCF_CHANGE_CONNECTION_TYPE;
    hci_cmd_buf[1] = HCI_OGF_LINK_CNTRL;
    hci_cmd_buf[2] = 0x04;                       // parameter length 4
    hci_cmd_buf[3] = (u8)(handle & 0xFF);        // connection handle - low byte
    hci_cmd_buf[4] = (u8)((handle >> 8) & 0x0F); // connection handle - high byte
    hci_cmd_buf[5] = 0x18;                       // Packet Type: 0xcc18
    hci_cmd_buf[6] = 0xcc;

    return HCI_Command(7, hci_cmd_buf);
}

static int hci_link_key_request_reply(u8 *bdaddr)
{
    hci_cmd_buf[0] = HCI_OCF_LINK_KEY_REQUEST_REPLY; // HCI OCF = 0E
    hci_cmd_buf[1] = HCI_OGF_LINK_CNTRL;             // HCI OGF = 1
    hci_cmd_buf[2] = 0x06 + sizeof(link_key);        // parameter length 6
    hci_cmd_buf[3] = *bdaddr;                        // 6 octet bluetooth address
    hci_cmd_buf[4] = *(bdaddr + 1);
    hci_cmd_buf[5] = *(bdaddr + 2);
    hci_cmd_buf[6] = *(bdaddr + 3);
    hci_cmd_buf[7] = *(bdaddr + 4);
    hci_cmd_buf[8] = *(bdaddr + 5);

    mips_memcpy(&hci_cmd_buf[9], link_key, sizeof(link_key));

    return HCI_Command(9 + sizeof(link_key), hci_cmd_buf);
}


static int hci_link_key_reply_value(const u8 *bdaddr, const u8 *key)
{
    hci_cmd_buf[0] = HCI_OCF_LINK_KEY_REQUEST_REPLY;
    hci_cmd_buf[1] = HCI_OGF_LINK_CNTRL;
    hci_cmd_buf[2] = 22;
    mips_memcpy(hci_cmd_buf + 3, bdaddr, 6);
    mips_memcpy(hci_cmd_buf + 9, key, 16);
    return HCI_Command(25, hci_cmd_buf);
}

static int hci_link_key_negative_reply(const u8 *bdaddr)
{
    hci_cmd_buf[0] = HCI_OCF_LINK_KEY_REQUEST_NEG_REPLY;
    hci_cmd_buf[1] = HCI_OGF_LINK_CNTRL;
    hci_cmd_buf[2] = 6;
    mips_memcpy(hci_cmd_buf + 3, bdaddr, 6);
    return HCI_Command(9, hci_cmd_buf);
}

static int hci_set_titan_event_mask(void)
{
    /* HCI Reset restores the default event mask (bits 0..44). SSP events
     * live above that range, so explicitly enable the events required by
     * Titan pairing while preserving LE Meta for the existing Xbox HOGP
     * backend. Reserved/unsupported bits are deliberately left clear. */
    static const u8 mask[8] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x5F, /* default 40..44 + Extended Inquiry Result (46) */
        0x27, /* IO Capability Req/Resp + User Confirmation + SSP Complete */
        0x3C  /* Passkey/keypress/remote features + LE Meta */
    };
    hci_cmd_buf[0] = HCI_OCF_SET_EVENT_MASK;
    hci_cmd_buf[1] = HCI_OGF_CTRL_BBAND;
    hci_cmd_buf[2] = 8;
    mips_memcpy(hci_cmd_buf + 3, mask, sizeof(mask));
    return HCI_Command(11, hci_cmd_buf);
}

static int hci_write_simple_pairing_mode(void)
{
    hci_cmd_buf[0] = HCI_OCF_WRITE_SIMPLE_PAIRING_MODE;
    hci_cmd_buf[1] = HCI_OGF_CTRL_BBAND;
    hci_cmd_buf[2] = 1;
    hci_cmd_buf[3] = 1;
    return HCI_Command(4, hci_cmd_buf);
}

static int hci_inquiry(void)
{
    /* General/Unlimited Inquiry Access Code, ~5.12 seconds, unlimited results. */
    hci_cmd_buf[0] = HCI_OCF_INQUIRY;
    hci_cmd_buf[1] = HCI_OGF_LINK_CNTRL;
    hci_cmd_buf[2] = 5;
    hci_cmd_buf[3] = 0x33;
    hci_cmd_buf[4] = 0x8B;
    hci_cmd_buf[5] = 0x9E;
    hci_cmd_buf[6] = 0x04;
    hci_cmd_buf[7] = 0x00;
    return HCI_Command(8, hci_cmd_buf);
}

void ds34bt_note_pending_wireless_command(u32 command)
{
    if (command == TITAN_PADEMU_WIRELESS_BEGIN_SCAN) {
        g_wireless_bootstrap_scan_pending = 1;
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_UI, 0,
                            0x50313034u, command, 1u); /* "P104" */
    }
}

static int wireless_start_classic_scan_sequence(void)
{
    int rc;
    if (!g_wireless_scan_requested || !sony_host_bdaddr_valid || !g_wireless_hci_startup_ready)
        return 0;

    /* Wireless Accessories shares one HCI command lane with Xbox HOGP. If the
     * background LE scanner is active, stop it and wait for LE Set Scan Enable
     * Command Complete before submitting any Classic controller command. */
    if (xbox_ble.state == TITAN_BLE_SCANNING) {
        rc = TitanBleHogp_StopScan(&xbox_ble);
        if (!rc) {
            g_wireless.last_error = -712;
            g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
            g_wireless.flags &= ~TITAN_PADEMU_WIRELESS_SCANNING;
            g_wireless_scan_requested = 0;
            wireless_state_changed();
            return 0;
        }
        g_wireless_wait_ble_stop = 1;
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_DISCOVERY, 0, TITAN_WIRELESS_BA(1), 5u, 0u);
        return 1;
    }
    if (g_wireless_wait_ble_stop) return 1;

    g_wireless_ssp_event_mask = 0;
    g_wireless_ssp_mode = 0;
    g_wireless_scan_pending = 1;
    rc = hci_set_titan_event_mask();
    if (rc >= 0) return 1;

    /* Discovery remains useful if an old controller rejects the extended event
     * mask, but SSP_READY stays clear and a new unbonded device cannot be
     * selected as if secure pairing were available. */
    g_wireless_scan_pending = 2;
    rc = hci_write_simple_pairing_mode();
    if (rc >= 0) return 1;

    g_wireless_scan_pending = 0;
    rc = hci_inquiry();
    if (rc >= 0) return 1;
    g_wireless.last_error = -702;
    g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
    g_wireless.flags &= ~TITAN_PADEMU_WIRELESS_SCANNING;
    g_wireless_scan_requested = 0;
    wireless_state_changed();
    return 0;
}

static int hci_remote_name_accessory(const TitanPademuWirelessAccessory *a)
{
    u16 clock;
    if (!a) return -1;
    hci_cmd_buf[0] = HCI_OCF_REMOTE_NAME;
    hci_cmd_buf[1] = HCI_OGF_LINK_CNTRL;
    hci_cmd_buf[2] = 10;
    mips_memcpy(hci_cmd_buf + 3, a->address, 6);
    hci_cmd_buf[9] = a->page_scan_repetition_mode ? a->page_scan_repetition_mode : 1;
    hci_cmd_buf[10] = 0;
    clock = a->clock_offset;
    hci_cmd_buf[11] = (u8)(clock & 0xff);
    hci_cmd_buf[12] = (u8)(clock >> 8);
    return HCI_Command(13, hci_cmd_buf);
}

static int hci_create_accessory_connection(const TitanPademuWirelessAccessory *a)
{
    u16 clock;
    if (!a) return -1;
    hci_cmd_buf[0] = HCI_OCF_CREATE_CONNECTION;
    hci_cmd_buf[1] = HCI_OGF_LINK_CNTRL;
    hci_cmd_buf[2] = 13;
    mips_memcpy(hci_cmd_buf + 3, a->address, 6);
    hci_cmd_buf[9] = 0x18; hci_cmd_buf[10] = 0xCC; /* ACL packet types */
    hci_cmd_buf[11] = a->page_scan_repetition_mode ? a->page_scan_repetition_mode : 1;
    hci_cmd_buf[12] = 0;
    clock = a->clock_offset;
    hci_cmd_buf[13] = (u8)(clock & 0xff);
    hci_cmd_buf[14] = (u8)(clock >> 8);
    hci_cmd_buf[15] = 1; /* allow role switch */
    return HCI_Command(16, hci_cmd_buf);
}

static int hci_authentication_requested(u16 handle)
{
    hci_cmd_buf[0] = HCI_OCF_AUTHENTICATION_REQUESTED;
    hci_cmd_buf[1] = HCI_OGF_LINK_CNTRL;
    hci_cmd_buf[2] = 2;
    hci_cmd_buf[3] = (u8)(handle & 0xff);
    hci_cmd_buf[4] = (u8)((handle >> 8) & 0x0f);
    return HCI_Command(5, hci_cmd_buf);
}

static int hci_io_capability_reply(const u8 *bdaddr)
{
    hci_cmd_buf[0] = HCI_OCF_IO_CAPABILITY_REPLY;
    hci_cmd_buf[1] = HCI_OGF_LINK_CNTRL;
    hci_cmd_buf[2] = 9;
    mips_memcpy(hci_cmd_buf + 3, bdaddr, 6);
    hci_cmd_buf[9] = 0x03; /* NoInputNoOutput */
    hci_cmd_buf[10] = 0x00; /* no OOB data */
    hci_cmd_buf[11] = 0x04; /* general bonding, no MITM */
    return HCI_Command(12, hci_cmd_buf);
}

static int hci_user_confirmation_reply(const u8 *bdaddr)
{
    hci_cmd_buf[0] = HCI_OCF_USER_CONFIRM_REPLY;
    hci_cmd_buf[1] = HCI_OGF_LINK_CNTRL;
    hci_cmd_buf[2] = 6;
    mips_memcpy(hci_cmd_buf + 3, bdaddr, 6);
    return HCI_Command(9, hci_cmd_buf);
}

static int wireless_find_pad(const u8 address[6])
{
    int i;
    for (i = 0; i < MAX_PADS; i++)
        if (wireless_addr_equal(ds34pad[i].bdaddr, address)) return i;
    return -1;
}

static int wireless_alloc_pad(const u8 address[6])
{
    int i;
    for (i = 0; i < MAX_PADS; i++) {
        if (wireless_addr_equal(ds34pad[i].bdaddr, address)) return i;
    }
    for (i = 0; i < MAX_PADS; i++) {
        if (!pad_status_check(DS34BT_STATE_RUNNING, i) && ds34pad[i].enabled) {
            if (pad_status_check(DS34BT_STATE_CONNECTED, i) &&
                !pad_status_check(DS34BT_STATE_DISCONNECTING, i))
                (void)hci_disconnect(ds34pad[i].hci_handle);
            ds34pad_clear(i);
            mips_memcpy(ds34pad[i].bdaddr, address, 6);
            ds34pad[i].isfake = 0;
            return i;
        }
    }
    return -1;
}

static void wireless_mark_connected(const u8 address[6], int connected)
{
    int wi = wireless_accessory_find(address);
    if (wi < 0) return;
    if (connected) {
        g_wireless.accessories[wi].flags |= TITAN_PADEMU_WIRELESS_CONNECTED;
        g_wireless.accessories[wi].flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTING;
    } else {
        g_wireless.accessories[wi].flags &= (u8)~(TITAN_PADEMU_WIRELESS_CONNECTED | TITAN_PADEMU_WIRELESS_CONNECTING);
    }
    wireless_state_changed();
}

static void wireless_resolve_next_name(void)
{
    unsigned i;
    for (i = 0; i < g_wireless.count; i++) {
        TitanPademuWirelessAccessory *a = &g_wireless.accessories[i];
        if ((a->flags & TITAN_PADEMU_WIRELESS_SEEN) && !a->name[0]) {
            g_wireless_name_index = (int)i;
            if (hci_remote_name_accessory(a) >= 0) return;
            a->name[0] = '?'; a->name[1] = 0;
        }
    }
    g_wireless_name_index = -1;
    g_wireless.flags &= ~TITAN_PADEMU_WIRELESS_SCANNING;
    g_wireless_scan_requested = 0;
    wireless_state_changed();
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_DISCOVERY, 0, TITAN_WIRELESS_BA(5), g_wireless.count, 0u);
    /* Classic discovery is complete. Probe/resume the Xbox BLE lane only when
     * the adapter actually supports LE HCI commands. BR/EDR-only dongles stay
     * fully usable for DualSense/DS4/DS3 without a dead BLE dependency. */
    if (xbox_ble.state == TITAN_BLE_OFF || xbox_ble.state == TITAN_BLE_FAILED) {
        if (g_xbox_ble_hci_capable < 0) {
            pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_DISCOVERY, 0,
                                TITAN_WIRELESS_BA(6), 0x00002018u, 0u);
        } else if (xbox_ble_random_count >= 16) {
            if (!TitanBleHogp_StartEarlyReconnect(&xbox_ble))
                (void)TitanBleHogp_StartScan(&xbox_ble);
        } else {
            g_xbox_ble_resume_pending = 1;
            xbox_ble_request_rand();
        }
    }
}

static void wireless_add_inquiry_result(const u8 *address, u8 page_scan, u32 class_of_device, u16 clock)
{
    int wi = wireless_accessory_get_or_add(address);
    TitanPademuWirelessAccessory *a;
    int bi;
    if (wi < 0) return;
    a = &g_wireless.accessories[wi];
    a->flags |= TITAN_PADEMU_WIRELESS_SEEN;
    if ((class_of_device & 0x1f00u) == 0x0500u) a->flags |= TITAN_PADEMU_WIRELESS_CONTROLLER;
    a->class_of_device = class_of_device;
    a->page_scan_repetition_mode = page_scan;
    a->clock_offset = clock;
    bi = wireless_bond_find(address);
    if (bi >= 0) {
        a->flags |= TITAN_PADEMU_WIRELESS_PAIRED | TITAN_PADEMU_WIRELESS_CONTROLLER;
        a->device_type = g_classic_bonds.entries[bi].device_type;
        if (!a->name[0]) wireless_copy_name(a->name, sizeof(a->name), g_classic_bonds.entries[bi].name);
    }
    wireless_state_changed();
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_DISCOVERY, 0, TITAN_WIRELESS_BA(3), (u32)wi, class_of_device);
}

static void wireless_parse_eir_name(TitanPademuWirelessAccessory *a, const u8 *eir, int bytes)
{
    int off = 0;
    if (!a || !eir || bytes <= 0) return;
    while (off + 1 < bytes) {
        int len = eir[off];
        int type;
        int copy;
        if (len <= 0 || off + len >= bytes) break;
        type = eir[off + 1];
        if ((type == 0x09 || type == 0x08) && len > 1) {
            copy = len - 1;
            if (copy >= (int)sizeof(a->name)) copy = (int)sizeof(a->name) - 1;
            mips_memcpy(a->name, eir + off + 2, copy);
            a->name[copy] = 0;
            return;
        }
        off += len + 1;
    }
}

static void HCI_event_task(int result)
{
    int i, pad;

    if (!result) {
        /*  buf[0] = Event Code                            */
        /*  buf[1] = Parameter Total Length                */
        /*  buf[n] = Event Parameters based on each event  */
        DPRINTF("HCI event = 0x%02X \n", hci_buf[0]);
        g_bt_transport.last_hci_event = hci_buf[0];
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_HCI_EVENT, 0, (u32)hci_buf[0],
                            (u32)(hci_buf[1]), (u32)((hci_buf[1] >= 1) ? hci_buf[2] : 0));
        switch (hci_buf[0]) { // switch on event type
            case HCI_EVENT_COMMAND_COMPLETE:
                if (hci_buf[3] == 0x18 && hci_buf[4] == 0x20) {
                    /* LE Rand is optional for the Classic Sony path. Some older
                     * CSR-style dongles expose BR/EDR perfectly but reject all
                     * LE opcodes. Never gate Classic Inquiry on LE entropy. */
                    if (hci_buf[5] == 0 && hci_buf[1] >= 12 && xbox_ble_random_count <= 24) {
                        g_xbox_ble_hci_capable = 1;
                        mips_memcpy(xbox_ble_random_pool + xbox_ble_random_count, hci_buf + 6, 8);
                        xbox_ble_random_count += 8;
                        if (xbox_ble_random_count < 16) {
                            xbox_ble_request_rand();
                        } else if (xbox_ble_random_count == 16) {
                            g_wireless_hci_startup_ready = 1;
                            if (g_wireless_scan_requested) {
                                (void)wireless_start_classic_scan_sequence();
                            } else if (g_xbox_ble_resume_pending) {
                                g_xbox_ble_resume_pending = 0;
                                if (!TitanBleHogp_StartEarlyReconnect(&xbox_ble))
                                    TitanBleHogp_StartScan(&xbox_ble);
                            } else if (!TitanBleHogp_StartEarlyReconnect(&xbox_ble)) {
                                TitanBleHogp_StartScan(&xbox_ble);
                            }
                        }
                    } else {
                        /* Command Complete with only status (plen=4) is the
                         * signature seen on the user's 0A12:0001 adapter. Keep
                         * Classic BR/EDR available and fail only the BLE lane. */
                        g_xbox_ble_hci_capable = -1;
                        g_xbox_ble_resume_pending = 0;
                        xbox_ble_random_count = 0;
                        g_wireless_hci_startup_ready = 1;
                        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_DISCOVERY,
                                            (int)hci_buf[5], TITAN_WIRELESS_BA(6),
                                            0x00002018u, (u32)hci_buf[1]);
                        if (g_wireless_scan_requested)
                            (void)wireless_start_classic_scan_sequence();
                    }
                } else if (hci_buf[3] == 0x09 && hci_buf[4] == (0x04<<2) && hci_buf[5] == 0) {
                    mips_memcpy(xbox_ble.local, hci_buf + 6, 6); xbox_ble.local_type = 0;
                    mips_memcpy(sony_host_bdaddr, hci_buf + 6, 6);
                    sony_host_bdaddr_valid = 1;
                    pademu_trace_record(TITAN_PADEMU_TRACE_BT_PAIR_REQUEST, 0,
                                        0x53500001u,
                                        (u32)sony_host_bdaddr[0] | ((u32)sony_host_bdaddr[1] << 8) |
                                        ((u32)sony_host_bdaddr[2] << 16) | ((u32)sony_host_bdaddr[3] << 24),
                                        (u32)sony_host_bdaddr[4] | ((u32)sony_host_bdaddr[5] << 8));
                    xbox_ble_random_count = 0;
                    if (g_wireless_scan_requested || g_wireless_bootstrap_scan_pending) {
                        /* User-visible discovery gets priority. P10.4: BEGIN_SCAN
                         * may still live in the SIF RPC pending slot while PAD_INIT
                         * is finishing. The bootstrap hint closes that ownership
                         * race: make Classic ready now and let the deferred command
                         * submit SSP/Inquiry as soon as the RPC pump reaches
                         * ds34bt_wireless_command(). */
                        g_wireless_hci_startup_ready = 1;
                        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_DISCOVERY, 0,
                                            0x50313034u, 0x00001009u,
                                            (u32)g_wireless_bootstrap_scan_pending);
                        if (g_wireless_scan_requested)
                            (void)wireless_start_classic_scan_sequence();
                    } else {
                        g_wireless_hci_startup_ready = 0;
                        g_xbox_ble_resume_pending = 1;
                        xbox_ble_request_rand();
                    }
                    if (sony_pair_requested)
                        (void)sony_pair_submit();
                } else if (hci_buf[3] == 0x0c && hci_buf[4] == 0x20 && g_wireless_wait_ble_stop) {
                    int wait_reason = g_wireless_wait_ble_stop;
                    g_wireless_wait_ble_stop = 0;
                    pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_DISCOVERY, (int)hci_buf[5],
                                        TITAN_WIRELESS_BA(1), 6u, (u32)wait_reason);
                    if (hci_buf[5] != 0) {
                        g_wireless.last_error = -(0x680 + (int)hci_buf[5]);
                        g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
                        if (wait_reason == 1) {
                            g_wireless.flags &= ~TITAN_PADEMU_WIRELESS_SCANNING;
                            g_wireless_scan_requested = 0;
                        } else if (wait_reason == 2) {
                            if (g_wireless.selected < g_wireless.count)
                                g_wireless.accessories[g_wireless.selected].flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTING;
                            g_wireless_direct_pad = -1;
                        }
                        wireless_state_changed();
                    } else if (wait_reason == 1) {
                        (void)wireless_start_classic_scan_sequence();
                    } else if (wait_reason == 2) {
                        int submit_rc = -1;
                        if (g_wireless.selected < g_wireless.count && g_wireless_direct_pad >= 0)
                            submit_rc = hci_create_accessory_connection(&g_wireless.accessories[g_wireless.selected]);
                        if (submit_rc < 0) {
                            g_wireless.last_error = -707;
                            g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
                            if (g_wireless.selected < g_wireless.count)
                                g_wireless.accessories[g_wireless.selected].flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTING;
                            g_wireless_direct_pad = -1;
                            wireless_state_changed();
                        }
                    }
                } else if (hci_buf[3] == HCI_OCF_SET_EVENT_MASK &&
                           hci_buf[4] == HCI_OGF_CTRL_BBAND && g_wireless_scan_pending == 1) {
                    int ssp_rc;
                    g_wireless_ssp_event_mask = hci_buf[5] == 0;
                    g_wireless_scan_pending = 2;
                    ssp_rc = hci_write_simple_pairing_mode();
                    pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING,
                                        (int)hci_buf[5], TITAN_WIRELESS_BA(1),
                                        (u32)g_wireless_ssp_event_mask, (u32)ssp_rc);
                    if (ssp_rc < 0) {
                        /* Discovery still has value on old controllers, but do not
                         * advertise SSP readiness unless both setup stages succeed. */
                        g_wireless_scan_pending = 0;
                        g_wireless_ssp_mode = 0;
                        if (hci_inquiry() >= 0)
                            g_wireless.flags |= TITAN_PADEMU_WIRELESS_SCANNING;
                        else {
                            g_wireless.last_error = -702;
                            g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
                            g_wireless.flags &= ~TITAN_PADEMU_WIRELESS_SCANNING;
                        }
                    }
                    wireless_state_changed();
                } else if (hci_buf[3] == HCI_OCF_WRITE_SIMPLE_PAIRING_MODE &&
                           hci_buf[4] == HCI_OGF_CTRL_BBAND && g_wireless_scan_pending == 2) {
                    int inquiry_rc;
                    g_wireless_scan_pending = 0;
                    g_wireless_ssp_mode = hci_buf[5] == 0;
                    if (g_wireless_ssp_event_mask && g_wireless_ssp_mode)
                        g_wireless.flags |= TITAN_PADEMU_WIRELESS_SSP_READY;
                    else
                        g_wireless.flags &= ~TITAN_PADEMU_WIRELESS_SSP_READY;
                    inquiry_rc = hci_inquiry();
                    if (inquiry_rc >= 0) {
                        g_wireless.flags |= TITAN_PADEMU_WIRELESS_SCANNING;
                        g_wireless.flags &= ~TITAN_PADEMU_WIRELESS_HAS_ERROR;
                        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_DISCOVERY, inquiry_rc, TITAN_WIRELESS_BA(2),
                                            (u32)g_wireless_ssp_event_mask, (u32)g_wireless_ssp_mode);
                    } else {
                        g_wireless.last_error = inquiry_rc;
                        g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
                    }
                    wireless_state_changed();
                }
                DPRINTF("HCI Command Complete = 0x%02X \n", hci_buf[3]);
                DPRINTF("\tReturned = 0x%02X \n", hci_buf[5]);
                g_bt_transport.last_hci_opcode = (u32)hci_buf[3] | ((u32)hci_buf[4] << 8);
                g_bt_transport.last_hci_status = hci_buf[5];
                if ((hci_buf[3] == HCI_OCF_RESET) && (hci_buf[4] == HCI_OGF_CTRL_BBAND)) {
                    if (hci_buf[5] == 0) {
                        int scan_rc;
                        bt_diag_stage(TITAN_PADEMU_BT_STAGE_HCI_RESET_COMPLETE, 0);
                        scan_rc = hci_write_scan_enable(SCAN_ENABLE_NOINQ_ENPAG);
                        g_bt_transport.scan_submit_result = scan_rc;
                        if (scan_rc == USB_RC_OK) bt_diag_stage(TITAN_PADEMU_BT_STAGE_SCAN_ENABLE_SUBMITTED, 0);
                        else bt_diag_stage(TITAN_PADEMU_BT_STAGE_SCAN_ENABLE_FAILED, scan_rc);
                    } else {
                        int reset_rc;
                        g_bt_transport.last_error = -(0x300 + (int)hci_buf[5]);
                        bt_diag_stage(TITAN_PADEMU_BT_STAGE_HCI_RESET_FAILED, g_bt_transport.last_error);
                        DelayThread(500);
                        reset_rc = hci_reset();
                        g_bt_transport.reset_submit_result = reset_rc;
                        if (reset_rc == USB_RC_OK) g_bt_transport.stage = TITAN_PADEMU_BT_STAGE_HCI_RESET_SUBMITTED;
                    }
                } else if ((hci_buf[3] == HCI_OCF_WRITE_SCAN_ENABLE) && (hci_buf[4] == HCI_OGF_CTRL_BBAND)) {
                    if (hci_buf[5] == 0) {
                        bt_dev.status |= DS34BT_STATE_USB_CONFIGURED;
                        bt_diag_stage(TITAN_PADEMU_BT_STAGE_READY, 0);
                        /* P9.2: serialized HCI startup.
                         * WRITE_SCAN_ENABLE -> READ_BD_ADDR -> RAND -> RAND -> BLE scan. */
                        sony_host_bdaddr_valid = 0;
                        xbox_ble_random_count = 0;
                        hci_read_bdaddr();
} else {
                        int reset_rc;
                        g_bt_transport.last_error = -(0x400 + (int)hci_buf[5]);
                        bt_diag_stage(TITAN_PADEMU_BT_STAGE_SCAN_ENABLE_FAILED, g_bt_transport.last_error);
                        DelayThread(500);
                        reset_rc = hci_reset();
                        g_bt_transport.reset_submit_result = reset_rc;
                        if (reset_rc == USB_RC_OK) g_bt_transport.stage = TITAN_PADEMU_BT_STAGE_HCI_RESET_SUBMITTED;
                    }
                }
                break;

            case HCI_EVENT_COMMAND_STATUS:
                g_bt_transport.last_hci_status = hci_buf[2];
                g_bt_transport.last_hci_opcode = (u32)hci_buf[4] | ((u32)hci_buf[5] << 8);
                if (hci_buf[2]) { // show status on serial if not OK
                    g_bt_transport.last_error = -(0x500 + (int)hci_buf[2]);
                    DPRINTF("HCI Command Failed: \n");
                    DPRINTF("\t Status = 0x%02X \n", hci_buf[2]);
                    DPRINTF("\t Command_OpCode(OGF) = 0x%02X \n", ((hci_buf[5] & 0xFC) >> 2));
                    DPRINTF("\t Command_OpCode(OCF) = 0x%02X 0x%02X \n", (hci_buf[5] & 0x03), hci_buf[4]);
                    if (hci_buf[4] == HCI_OCF_CREATE_CONNECTION && hci_buf[5] == HCI_OGF_LINK_CNTRL) {
                        if (hci_buf[2] == 0x0B && g_wireless_direct_pad >= 0) {
                            /* Connection Already Exists: benign duplicate request.
                             * Preserve the live ACL/HID handshake instead of turning
                             * it into user-visible -1627 and dropping direct_pad. */
                            g_wireless.last_error = 0;
                            g_wireless.flags &= ~TITAN_PADEMU_WIRELESS_HAS_ERROR;
                            pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_UI, 0,
                                                TITAN_WIRELESS_BA(18), g_wireless.selected,
                                                (u32)g_wireless_direct_pad);
                            wireless_state_changed();
                        } else {
                            g_wireless.last_error = -(0x650 + (int)hci_buf[2]);
                            g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
                            if (g_wireless.selected < g_wireless.count)
                                g_wireless.accessories[g_wireless.selected].flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTING;
                            g_wireless_direct_pad = -1;
                            wireless_state_changed();
                        }
                    } else if (hci_buf[4] == HCI_OCF_INQUIRY && hci_buf[5] == HCI_OGF_LINK_CNTRL) {
                        g_wireless.last_error = -(0x660 + (int)hci_buf[2]);
                        g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
                        g_wireless.flags &= ~TITAN_PADEMU_WIRELESS_SCANNING;
                        wireless_state_changed();
                    } else if (hci_buf[4] == HCI_OCF_AUTHENTICATION_REQUESTED && hci_buf[5] == HCI_OGF_LINK_CNTRL) {
                        g_wireless.last_error = -(0x670 + (int)hci_buf[2]);
                        g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
                        if (g_wireless.selected < g_wireless.count)
                            g_wireless.accessories[g_wireless.selected].flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTING;
                        g_wireless_direct_pad = -1;
                        wireless_state_changed();
                    }
                }
                break;

            case HCI_EVENT_INQUIRY_RESULT:
            {
                unsigned n = hci_buf[2];
                unsigned k;
                unsigned addr_base = 3u;
                unsigned page_base = addr_base + (6u * n);
                unsigned reserved_base = page_base + n;
                unsigned class_base = reserved_base + n;
                unsigned clock_base = class_base + (3u * n);
                for (k = 0; k < n && k < TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES; k++) {
                    if ((int)(clock_base + 2u * k + 1u) >= g_hci_event_bytes) break;
                    wireless_add_inquiry_result(
                        hci_buf + addr_base + (6u * k),
                        hci_buf[page_base + k],
                        (u32)hci_buf[class_base + 3u * k] |
                        ((u32)hci_buf[class_base + 3u * k + 1u] << 8) |
                        ((u32)hci_buf[class_base + 3u * k + 2u] << 16),
                        (u16)hci_buf[clock_base + 2u * k] |
                        ((u16)hci_buf[clock_base + 2u * k + 1u] << 8));
                }
                break;
            }

            case HCI_EVENT_EXTENDED_INQUIRY_RESULT:
                if (g_hci_event_bytes >= 17) {
                    int wi;
                    u32 cod = (u32)hci_buf[11] | ((u32)hci_buf[12] << 8) | ((u32)hci_buf[13] << 16);
                    u16 clock = (u16)hci_buf[14] | ((u16)hci_buf[15] << 8);
                    wireless_add_inquiry_result(hci_buf + 3, hci_buf[9], cod, clock);
                    wi = wireless_accessory_find(hci_buf + 3);
                    if (wi >= 0 && g_hci_event_bytes > 17)
                        wireless_parse_eir_name(&g_wireless.accessories[wi], hci_buf + 17, g_hci_event_bytes - 17);
                }
                break;

            case HCI_EVENT_INQUIRY_COMPLETE:
                pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_DISCOVERY, hci_buf[2], TITAN_WIRELESS_BA(4), g_wireless.count, 0u);
                if (hci_buf[2]) {
                    g_wireless.last_error = -(0x610 + (int)hci_buf[2]);
                    g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
                }
                wireless_resolve_next_name();
                break;

            case HCI_EVENT_CONNECT_COMPLETE:
                pademu_trace_record(TITAN_PADEMU_TRACE_BT_HCI_CONNECT, (int)hci_buf[2],
                                    (u32)(hci_buf[3] | ((hci_buf[4] & 0x0F) << 8)),
                                    ((u32)hci_buf[5] << 24) | ((u32)hci_buf[6] << 16) | ((u32)hci_buf[7] << 8) | (u32)hci_buf[8],
                                    ((u32)hci_buf[9] << 8) | (u32)hci_buf[10]);
                DPRINTF("HCI event Connect Complete: \n");
                if (!hci_buf[2]) { // check if connected OK
                    DPRINTF("\t Connection_Handle 0x%02X \n", hci_buf[3] | ((hci_buf[4] & 0x0F) << 8));
                    DPRINTF("\t Requested by BD_ADDR: \n\t");
                    for (i = 0; i < 6; i++) {
                        DPRINTF("0x%02X", hci_buf[5 + i]);
                        if (i < 5)
                            DPRINTF(":");
                    }
                    DPRINTF("\n");
                    for (i = 0; i < MAX_PADS; i++) {
                        if (memcmp(ds34pad[i].bdaddr, hci_buf + 5, 6) == 0) {
                            // store the handle for the ACL connection
                            ds34pad[i].hci_handle = hci_buf[3] | ((hci_buf[4] & 0x0F) << 8);
                            break;
                        }
                    }
                    if (i >= MAX_PADS) {
                        break;
                    }
                    pad_status_set(DS34BT_STATE_CONNECTED, i);
                    { int wi = wireless_accessory_find(ds34pad[i].bdaddr);
                      if (wi >= 0) {
                          g_wireless.accessories[wi].flags |= TITAN_PADEMU_WIRELESS_CONNECTING;
                          g_wireless.accessories[wi].flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTED;
                          wireless_state_changed();
                      } }
                    pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0, TITAN_WIRELESS_BA(7), (u32)i, (u32)ds34pad[i].hci_handle);
                    hci_remote_name(ds34pad[i].bdaddr);
                } else {
                    DPRINTF("\t Error 0x%02X \n", hci_buf[2]);
                    g_wireless.last_error = -(0x620 + (int)hci_buf[2]);
                    g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
                    if (g_wireless.selected < g_wireless.count)
                        g_wireless.accessories[g_wireless.selected].flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTING;
                    g_wireless_direct_pad = -1;
                    wireless_state_changed();
                }
                break;

            case HCI_EVENT_NUM_COMPLETED_PKT:
                DPRINTF("HCI Number Of Completed Packets Event: \n");
                DPRINTF("\t Number_of_Handles = 0x%02X \n", hci_buf[2]);
                for (i = 0; i < hci_buf[2]; i++) {
                    DPRINTF("\t Connection_Handle = 0x%04X \n", (hci_buf[3 + i] | ((hci_buf[4 + i] & 0x0F) << 8)));
                }
                break;

            case HCI_EVENT_QOS_SETUP_COMPLETE:
                break;

            case 0x38: /* Link Supervision Timeout Changed */
                if (g_hci_event_bytes >= 7) {
                    u16 p1065_handle =
                        (u16)(hci_buf[3] | ((hci_buf[4] & 0x0F) << 8));
                    u16 p1065_timeout =
                        (u16)(hci_buf[5] | ((u16)hci_buf[6] << 8));
                    pademu_trace_record(TITAN_PADEMU_TRACE_BT_HCI_EVENT,
                                        (int)hci_buf[2],
                                        0x50313637u, /* P167 */
                                        (u32)p1065_handle,
                                        (u32)p1065_timeout);
                }
                break;

            case HCI_EVENT_DISCONN_COMPLETE:
                DPRINTF("HCI Disconnection Complete Event: \n");
                DPRINTF("\t Status = 0x%02X \n", hci_buf[2]);
                DPRINTF("\t Connection_Handle = 0x%04X \n", (hci_buf[3] | ((hci_buf[4] & 0x0F) << 8)));
                DPRINTF("\t Reason = 0x%02X \n", hci_buf[5]);
                if (g_hci_event_bytes >= 6) {
                    u16 p1064_handle =
                        (u16)(hci_buf[3] | ((hci_buf[4] & 0x0F) << 8));
                    pademu_trace_record(TITAN_PADEMU_TRACE_BT_HCI_EVENT,
                                        (int)hci_buf[2],
                                        0x50313635u, /* P165 */
                                        (u32)p1064_handle,
                                        (u32)hci_buf[5]);
                    pademu_trace_record(TITAN_PADEMU_TRACE_BT_HCI_EVENT,
                                        (int)hci_buf[2],
                                        0x50313638u, /* P168 */
                                        ((u32)((u16)g_p1065_l2cap_last_result) << 16) |
                                            (u32)((u16)g_p1065_l2cap_last_bytes),
                                        g_p1065_l2cap_cb_count);
                }
                for (i = 0; i < MAX_PADS; i++) { // detect pad
                    if (ds34pad[i].hci_handle == (hci_buf[3] | ((hci_buf[4] & 0x0F) << 8))) {
                        break;
                    }
                }
                if (i < MAX_PADS) wireless_mark_connected(ds34pad[i].bdaddr, 0);
                if (i == g_wireless_l2cap_watch_pad) {
                    g_wireless_l2cap_watch_pad = -1;
                    g_wireless_l2cap_watch_queries = 0u;
                    g_wireless_l2cap_retry_count = 0u;
                }
                ds34pad_clear(i);
                if (i == g_wireless_direct_pad) g_wireless_direct_pad = -1;
                break;

            case HCI_EVENT_AUTHENTICATION_COMPLETE:
            {
                u16 handle = (u16)(hci_buf[3] | ((hci_buf[4] & 0x0F) << 8));
                DPRINTF("HCI Authentication Complete Event: \n");
                DPRINTF("\t Status = 0x%02X \n", hci_buf[2]);
                if (g_wireless_direct_pad >= 0 && g_wireless_direct_pad < MAX_PADS &&
                    ds34pad[g_wireless_direct_pad].hci_handle == handle) {
                    if (!hci_buf[2]) {
                        int dp = g_wireless_direct_pad;
                        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0, TITAN_WIRELESS_BA(12), (u32)dp, handle);
                        hci_change_connection_type(handle);
                        if (ds34pad[dp].type != DS4 && !ds34pad[dp].sony_generation_pending) {
                            int wi = wireless_accessory_find(ds34pad[dp].bdaddr);
                            if (wi >= 0 &&
                                wireless_is_sony_controller_name(g_wireless.accessories[wi].name)) {
                                ds34pad[dp].type = DS4;
                                ds34pad[dp].sony_generation_pending = 1;
                                pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0,
                                                    TITAN_WIRELESS_BA(19),
                                                    (u32)dp | ((u32)DS4 << 8) | (1u << 16),
                                                    0x43414348u);
                            }
                        }
                        if (ds34pad[dp].type == DS4 || ds34pad[dp].sony_generation_pending) {
                            int l2cap_rc;
                            identifier++;
                            l2cap_rc = l2cap_connection_request(handle, identifier, 0x0070, L2CAP_PSM_CTRL);
                            /* The USB OUT endpoint can transiently reject a submit while
                             * HCI is completing SSP.  Do not recreate the ACL.  Keep a
                             * bounded one-shot L2CAP retry armed until the first ACL RX. */
                            g_wireless_l2cap_watch_pad = dp;
                            g_wireless_l2cap_watch_queries = 0u;
                            g_wireless_l2cap_retry_count = 0u;
                            if (l2cap_rc < 0)
                                pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, l2cap_rc,
                                                    TITAN_WIRELESS_BA(17), (u32)dp, (u32)handle);
                        }
                        /* Authentication succeeded, but keep CONNECT_IN_PROGRESS
                         * asserted until both HID L2CAP channels reach RUNNING. */
                        wireless_state_changed();
                    } else {
                        g_wireless.last_error = -(0x630 + (int)hci_buf[2]);
                        g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
                        if (g_wireless.selected < g_wireless.count)
                            g_wireless.accessories[g_wireless.selected].flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTING;
                        g_wireless_direct_pad = -1;
                        wireless_state_changed();
                    }
                }
                break;
            }

            case HCI_EVENT_REMOTE_NAME_COMPLETE:
                if (g_wireless_name_index >= 0 && g_wireless_name_index < (int)g_wireless.count &&
                    wireless_addr_equal(g_wireless.accessories[g_wireless_name_index].address, hci_buf + 3)) {
                    TitanPademuWirelessAccessory *wa = &g_wireless.accessories[g_wireless_name_index];
                    if (!hci_buf[2]) {
                        wireless_copy_name(wa->name, sizeof(wa->name), (const char *)(hci_buf + 9));
                        if (wireless_is_sony_controller_name(wa->name))
                            wa->flags |= TITAN_PADEMU_WIRELESS_CONTROLLER;
                    } else if (!wa->name[0]) {
                        wireless_copy_name(wa->name, sizeof(wa->name), "Bluetooth accessory");
                    }
                    pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_DISCOVERY, (int)hci_buf[2], TITAN_WIRELESS_BA(5), (u32)g_wireless_name_index, 0u);
                    g_wireless_name_index = -1;
                    wireless_state_changed();
                    wireless_resolve_next_name();
                    break;
                }
                DPRINTF("HCI Remote Name Requested Complete Event: \n");
                DPRINTF("\t Status = 0x%02X \n", hci_buf[2]);
                if (!hci_buf[2]) {
                    for (i = 0; i < MAX_PADS; i++) {
                        if (memcmp(ds34pad[i].bdaddr, hci_buf + 3, 6) == 0) {
                            break;
                        }
                    }
                    if (i >= MAX_PADS) {
                        break;
                    }
                    if (wireless_is_sony_controller_name((const char *)(hci_buf + 9))) {
                        /* DS4 and DualSense both advertise "Wireless Controller".
                         * Keep the existing DS4-compatible L2CAP channel layout, but defer
                         * generation-specific HID feature/output commands until input report
                         * 0x11 (DS4) or 0x31 (DualSense) proves the device generation. */
                        ds34pad[i].type = DS4;
                        ds34pad[i].sony_generation_pending = 1;
                        ds34pad[i].isfake = 0;
                        DPRINTF("\t Type: Sony Wireless Controller (generation pending) \n");
                        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0,
                                            TITAN_WIRELESS_BA(19),
                                            (u32)i | ((u32)ds34pad[i].type << 8) |
                                                ((u32)ds34pad[i].sony_generation_pending << 16),
                                            (u32)(u8)hci_buf[9] |
                                                ((u32)(u8)hci_buf[10] << 8) |
                                                ((u32)(u8)hci_buf[11] << 16) |
                                                ((u32)(u8)hci_buf[12] << 24));
                    } else {
                        ds34pad[i].type = DS3;
                        DPRINTF("\t Type: Dualshock 3 \n");
                    }
                    if (i == g_wireless_direct_pad) {
                        int wi = wireless_accessory_find(ds34pad[i].bdaddr);
                        if (wi >= 0) {
                            g_wireless.accessories[wi].device_type = (ds34pad[i].type == DS3) ? TITAN_PADEMU_DEVICE_DS3 : TITAN_PADEMU_DEVICE_NONE;
                            wireless_copy_name(g_wireless.accessories[wi].name, sizeof(g_wireless.accessories[wi].name), (const char *)(hci_buf + 9));
                            wireless_state_changed();
                        }
                        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0, TITAN_WIRELESS_BA(8), (u32)i, (u32)ds34pad[i].hci_handle);
                        hci_authentication_requested(ds34pad[i].hci_handle);
                    } else {
                        hci_change_connection_type(ds34pad[i].hci_handle);
                        if (ds34pad[i].type == DS4) {
                            identifier++;
                            l2cap_connection_request(ds34pad[i].hci_handle, identifier, 0x0070, L2CAP_PSM_CTRL);
                        }
                    }
                }
                break;

            case HCI_EVENT_ENCRYPTION_CHANGE:
                DPRINTF("HCI Encryption Change Event: \n");
                DPRINTF("\t Status = 0x%02X \n", hci_buf[2]);
                DPRINTF("\t Connection_Handle = 0x%04X \n", (hci_buf[3] | ((hci_buf[4] & 0x0F) << 8)));
                DPRINTF("\t Encryption_Enabled = 0x%02X \n", hci_buf[5]);
                break;

            case HCI_EVENT_CONNECT_REQUEST:
                DPRINTF("HCI Connection Requested by BD_ADDR: \n\t");
                for (i = 0; i < 6; i++) {
                    DPRINTF("0x%02X", hci_buf[2 + i]);
                    if (i < 5)
                        DPRINTF(":");
                }
                DPRINTF("\n\t Link = 0x%02X \n", hci_buf[11]);
                DPRINTF("\t Class = 0x%02X 0x%02X 0x%02X \n", hci_buf[8], hci_buf[9], hci_buf[10]);
                for (i = 0; i < MAX_PADS; i++) { // find free slot
                    if (!pad_status_check(DS34BT_STATE_RUNNING, i) && ds34pad[i].enabled) {
                        if (pad_status_check(DS34BT_STATE_CONNECTED, i)) {
                            if (pad_status_check(DS34BT_STATE_DISCONNECTING, i)) // if we're waiting for hci disconnect event
                                continue;
                            else
                                hci_disconnect(ds34pad[i].hci_handle); // try to disconnect
                        }
                        break;
                    }
                }
                if (i >= MAX_PADS) { // no free slot
                    hci_reject_connection(hci_buf + 2);
                    break;
                }
                pad = i;
                mips_memcpy(ds34pad[pad].bdaddr, hci_buf + 2, 6);
                ds34pad[pad].isfake = 0;
                if (enable_fake) {
                    ds34pad[pad].isfake = 1;                              // fake ds3
                    for (i = 0; i < sizeof(GenuineMacAddress) / 3; i++) { // check if ds3 is genuine
                        if (ds34pad[pad].bdaddr[5] == GenuineMacAddress[i][0] &&
                            ds34pad[pad].bdaddr[4] == GenuineMacAddress[i][1] &&
                            ds34pad[pad].bdaddr[3] == GenuineMacAddress[i][2]) {
                            ds34pad[pad].isfake = 0;
                            break;
                        }
                    }
                }
                pad_status_clear(DS34BT_STATE_CONNECTED, pad);
                pad_status_clear(DS34BT_STATE_RUNNING, pad);
                pad_status_clear(DS34BT_STATE_DISCONNECTING, pad);
                hci_accept_connection(ds34pad[pad].bdaddr);
                break;

            case HCI_EVENT_ROLE_CHANGED:
                DPRINTF("HCI Role Change Event: \n");
                DPRINTF("\t Status = 0x%02X \n", hci_buf[2]);
                DPRINTF("\t BD_ADDR: ");
                for (i = 0; i < 6; i++) {
                    DPRINTF("0x%02X", hci_buf[3 + i]);
                    if (i < 5)
                        DPRINTF(":");
                }
                DPRINTF("\n\t Role 0x%02X \n", hci_buf[9]);
                break;

            case HCI_EVENT_MAX_SLOT_CHANGE:
                DPRINTF("HCI Max Slot Change Event: \n");
                DPRINTF("\t Connection_Handle = 0x%x \n", (hci_buf[2] | ((hci_buf[3] & 0x0F) << 8)));
                DPRINTF("\t LMP Max Slots = 0x%x \n", hci_buf[5]);
                break;

            case HCI_EVENT_PIN_CODE_REQUEST:
                DPRINTF("HCI Pin Code Request Event \n");
                break;

            case HCI_EVENT_LINK_KEY_REQUEST:
                DPRINTF("HCI Link Key Request Event by BD_ADDR: \n\t");
                for (i = 0; i < 6; i++) {
                    DPRINTF("0x%02X", hci_buf[2 + i]);
                    if (i < 5)
                        DPRINTF(":");
                }
                DPRINTF("\n");
                {
                    int bi = wireless_bond_find(hci_buf + 2);
                    if (bi >= 0) {
                        hci_link_key_reply_value(hci_buf + 2, g_classic_bonds.entries[bi].key);
                        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0, TITAN_WIRELESS_BA(10), (u32)bi, 1u);
                    } else if (g_wireless_direct_pad >= 0 && g_wireless_direct_pad < MAX_PADS &&
                               wireless_addr_equal(ds34pad[g_wireless_direct_pad].bdaddr, hci_buf + 2)) {
                        hci_link_key_negative_reply(hci_buf + 2);
                        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0, TITAN_WIRELESS_BA(10), 0xffffffffu, 0u);
                    } else {
                        /* Preserve OPL/legacy DS4 USB-provisioned semantics. */
                        hci_link_key_request_reply(hci_buf + 2);
                    }
                }
                break;

            case HCI_EVENT_LINK_KEY_NOTIFICATION:
                if (g_hci_event_bytes >= 25) {
                    wireless_store_key(hci_buf + 2, hci_buf + 8);
                    pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0, TITAN_WIRELESS_BA(11), (u32)hci_buf[24], 0u);
                }
                break;

            case HCI_EVENT_IO_CAPABILITY_REQUEST:
                if (g_hci_event_bytes >= 8) {
                    hci_io_capability_reply(hci_buf + 2);
                    pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0, TITAN_WIRELESS_BA(9), 0u, 0u);
                }
                break;

            case HCI_EVENT_USER_CONFIRMATION_REQUEST:
                if (g_hci_event_bytes >= 12) hci_user_confirmation_reply(hci_buf + 2);
                break;

            case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
                if (g_hci_event_bytes >= 9 && hci_buf[2]) {
                    g_wireless.last_error = -(0x640 + (int)hci_buf[2]);
                    g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
                    if (g_wireless.selected < g_wireless.count)
                        g_wireless.accessories[g_wireless.selected].flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTING;
                    g_wireless_direct_pad = -1;
                    wireless_state_changed();
                }
                break;

            case HCI_EVENT_CHANGED_CONNECTION_TYPE:
                DPRINTF("Packet Type Changed STATUS: 0x%x \n", hci_buf[2]);
                DPRINTF("\t Type = %x \n", (hci_buf[5] | (hci_buf[6] << 8)));
                break;

            case HCI_EVENT_PAGE_SR_CHANGED:
                break;

            default:
                DPRINTF("Unmanaged Event: 0x%02X \n", hci_buf[0]);
                break;
        } // switch (buf[0])
    }
}

static void ds34pad_clear(int pad)
{
    if (pad >= MAX_PADS)
        return;

    ds34pad[pad].hci_handle = 0x0FFF;
    ds34pad[pad].control_scid = 0;
    ds34pad[pad].interrupt_scid = 0;
    mips_memset(ds34pad[pad].bdaddr, 0, 6);
    ds34pad[pad].status = bt_dev.status;
    ds34pad[pad].status |= DS34BT_STATE_USB_CONFIGURED;
    ds34pad[pad].isfake = 0;
    ds34pad[pad].type = 0;
    ds34pad[pad].sony_generation_pending = 0;
    g_p1056_sony_feature05_requested[pad] = 0;
    ds34pad[pad].btn_delay = 0;
    ds34pad[pad].data[0] = 0xFF;
    ds34pad[pad].data[1] = 0xFF;
    mips_memset(&ds34pad[pad].data[2], 0x7F, 4);
    mips_memset(&ds34pad[pad].data[6], 0x00, 12);
}

static void ds34pad_init()
{
    int i;

    for (i = 0; i < MAX_PADS; i++) {
        ds34pad_clear(i);
    }

    g_press_emu = 0;
    identifier = 0;
    g_p1056_l2cap_hid_rx_packets = 0;
}

static void hci_event_cb(int resultCode, int bytes, void *arg)
{
    int rearm_rc;
    PollSema(bt_dev.hid_sema);
    if (resultCode != USB_RC_OK)
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_HCI_EVENT, resultCode, 0xffffffffu, (u32)bytes, 0u);
    g_hci_event_bytes = bytes > 0 ? bytes : MAX_BUFFER_SIZE;
    if (!resultCode) TitanBleHogp_OnHciEvent(&xbox_ble,hci_buf,(unsigned)g_hci_event_bytes);
    HCI_event_task(resultCode);
    p1064_l2cap_rx_recover_if_needed_locked(1u);
    rearm_rc = UsbInterruptTransfer(bt_dev.interruptEndp, hci_buf, MAX_BUFFER_SIZE, hci_event_cb, NULL);
    if (rearm_rc != USB_RC_OK)
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_USB_RX_ARM, rearm_rc, 1u, (u32)bt_dev.interruptEndp, 0u);
    SignalSema(bt_dev.hid_sema);
}

/************************************************************/
/* L2CAP Commands                                           */
/************************************************************/

static int L2CAP_Command(u16 handle, u8 *data, u8 length)
{
    l2cap_cmd_buf[0] = (u8)(handle & 0xff); // HCI handle with PB,BC flag
    l2cap_cmd_buf[1] = (u8)(((handle >> 8) & 0x0f) | 0x20);
    l2cap_cmd_buf[2] = (u8)((4 + length) & 0xff); // HCI ACL total data length
    l2cap_cmd_buf[3] = (u8)((4 + length) >> 8);
    l2cap_cmd_buf[4] = (u8)(length & 0xff); // L2CAP header: Length
    l2cap_cmd_buf[5] = (u8)(length >> 8);
    l2cap_cmd_buf[6] = 0x01; // L2CAP header: Channel ID
    l2cap_cmd_buf[7] = 0x00; // L2CAP Signalling channel over ACL-U logical link

    mips_memcpy(&l2cap_cmd_buf[8], data, length);

    // output on endpoint 2
    return UsbBulkTransfer(bt_dev.outEndp, l2cap_cmd_buf, (8 + length), NULL, NULL);
}

static int l2cap_connection_request(u16 handle, u8 rxid, u16 scid, u16 psm)
{
    u8 cmd_buf[8];
    int rc;

    cmd_buf[0] = L2CAP_CMD_CONNECTION_REQUEST; // Code
    cmd_buf[1] = rxid;                         // Identifier
    cmd_buf[2] = 0x04;                         // Length
    cmd_buf[3] = 0x00;
    cmd_buf[4] = (u8)(psm & 0xff); // PSM
    cmd_buf[5] = (u8)(psm >> 8);
    cmd_buf[6] = (u8)(scid & 0xff); // Source CID (PS Remote)
    cmd_buf[7] = (u8)(scid >> 8);

    rc = L2CAP_Command(handle, cmd_buf, 8);
    /* BA(13): L2CAP Connection Request TX.  result is the USBD submit rc;
     * b packs handle/rxid, c packs PSM/SCID.  This makes a no-RX failure
     * distinguishable from a callback/parser failure in one hardware dump. */
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, rc, TITAN_WIRELESS_BA(13),
                        ((u32)handle << 8) | (u32)rxid, ((u32)psm << 16) | (u32)scid);
    return rc;
}

static int l2cap_connection_response(u16 handle, u8 rxid, u16 dcid, u16 scid, u8 result)
{
    u8 cmd_buf[12];

    cmd_buf[0] = L2CAP_CMD_CONNECTION_RESPONSE; // Code
    cmd_buf[1] = rxid;                          // Identifier
    cmd_buf[2] = 0x08;                          // Length
    cmd_buf[3] = 0x00;
    cmd_buf[4] = (u8)(dcid & 0xff); // Destination CID (Our)
    cmd_buf[5] = (u8)(dcid >> 8);
    cmd_buf[6] = (u8)(scid & 0xff); // Source CID (PS Remote)
    cmd_buf[7] = (u8)(scid >> 8);
    cmd_buf[8] = result; // Result
    cmd_buf[9] = 0x00;
    cmd_buf[10] = 0x00; // Status
    cmd_buf[11] = 0x00;

    if (result != 0)
        cmd_buf[10] = 0x01; // Authentication pending

    return L2CAP_Command(handle, cmd_buf, 12);
}

static int l2cap_config_request(u16 handle, u8 rxid, u16 dcid)
{
    u8 cmd_buf[12];

    cmd_buf[0] = L2CAP_CMD_CONFIG_REQUEST; // Code
    cmd_buf[1] = rxid;                     // Identifier
    cmd_buf[2] = 0x08;                     // Length
    cmd_buf[3] = 0x00;
    cmd_buf[4] = (u8)(dcid & 0xff); // Destination CID
    cmd_buf[5] = (u8)(dcid >> 8);
    cmd_buf[6] = 0x00; // Flags
    cmd_buf[7] = 0x00;
    cmd_buf[8] = 0x01; // Config Opt: type = MTU (Maximum Transmission Unit)
    cmd_buf[9] = 0x02; // Config Opt: length
    // cmd_buf[10] = 0x96; // Config Opt: data
    // cmd_buf[11] = 0x00;

    // this setting disable hid cmd reports from ds3
    cmd_buf[10] = 0xFF; // Config Opt: data
    cmd_buf[11] = 0xFF;

    return L2CAP_Command(handle, cmd_buf, 12);
}

static int l2cap_config_response(u16 handle, u8 rxid, u16 scid)
{
    u8 cmd_buf[14];

    cmd_buf[0] = L2CAP_CMD_CONFIG_RESPONSE; // Code
    cmd_buf[1] = rxid;                      // Identifier
    cmd_buf[2] = 0x0A;                      // Length
    cmd_buf[3] = 0x00;
    cmd_buf[4] = (u8)(scid & 0xff); // Source CID
    cmd_buf[5] = (u8)(scid >> 8);
    cmd_buf[6] = 0x00; // Result
    cmd_buf[7] = 0x00;
    cmd_buf[8] = 0x00; // Config
    cmd_buf[9] = 0x00;
    cmd_buf[10] = 0x01; // Config
    cmd_buf[11] = 0x02;
    cmd_buf[12] = 0xA0;
    cmd_buf[13] = 0x02;

    return L2CAP_Command(handle, cmd_buf, 14);
}

static int l2cap_disconnection_request(u16 handle, u8 rxid, u16 dcid, u16 scid)
{
    u8 cmd_buf[8];

    cmd_buf[0] = L2CAP_CMD_DISCONNECT_REQUEST; // Code
    cmd_buf[1] = rxid;                         // Identifier
    cmd_buf[2] = 0x04;                         // Length
    cmd_buf[3] = 0x00;
    cmd_buf[4] = (u8)(dcid & 0xff); // Destination CID
    cmd_buf[5] = (u8)(dcid >> 8);
    cmd_buf[6] = (u8)(scid & 0xff); // Source CID
    cmd_buf[7] = (u8)(scid >> 8);

    return L2CAP_Command(handle, cmd_buf, 8);
}

static int l2cap_disconnection_response(u16 handle, u8 rxid, u16 scid, u16 dcid)
{
    u8 cmd_buf[8];

    cmd_buf[0] = L2CAP_CMD_DISCONNECT_RESPONSE; // Code
    cmd_buf[1] = rxid;                          // Identifier
    cmd_buf[2] = 0x04;                          // Length
    cmd_buf[3] = 0x00;
    cmd_buf[4] = (u8)(dcid & 0xff); // Destination CID
    cmd_buf[5] = (u8)(dcid >> 8);
    cmd_buf[6] = (u8)(scid & 0xff); // Source CID
    cmd_buf[7] = (u8)(scid >> 8);

    return L2CAP_Command(handle, cmd_buf, 8);
}

#define CMD_DELAY 2

static int L2CAP_event_task(int result, int bytes)
{
    if (!result && bytes >= 8) {
        u16 h=(u16)(l2cap_buf[0]|((l2cap_buf[1]&0x0f)<<8));
        u16 cid=(u16)(l2cap_buf[6]|(l2cap_buf[7]<<8));
        u16 plen=(u16)(l2cap_buf[4]|(l2cap_buf[5]<<8));
        if ((cid==TITAN_BLE_CID_ATT||cid==TITAN_BLE_CID_SMP) && plen <= (u16)(bytes-8)) { TitanBleHogp_OnAcl(&xbox_ble,h,cid,l2cap_buf+8,plen); return xbox_ble_pad>=0?xbox_ble_pad:MAX_PADS; }
    }

    int pad = -1;
    u16 control_dcid = 0x0040;   // Channel endpoint on command source
    u16 interrupt_dcid = 0x0041; // Channel endpoint on interrupt source

    if (!result) {
        for (pad = 0; pad < MAX_PADS; pad++) {
            if (l2cap_handle_ok(ds34pad[pad].hci_handle))
                break;
        }

        if (pad >= MAX_PADS) {
            DPRINTF("L2CAP Wrong Handle = 0x%04X\n", ((l2cap_buf[0] | (l2cap_buf[1] << 8))));
            return pad;
        }

        if (l2cap_handle_ok(ds34pad[pad].hci_handle)) {
            if (ds34pad[pad].type == DS4 || ds34pad[pad].type == DUALSENSE) {
                control_dcid = 0x0070;
                interrupt_dcid = 0x0071;
            }

            if (l2cap_control_channel) {
                DPRINTF("L2CAP Signaling Command = 0x%02X, pad %d \n", l2cap_buf[8], pad);

                switch (l2cap_buf[8]) {
                    case L2CAP_CMD_COMMAND_REJECT:
                        DPRINTF("Command Reject ID = 0x%02X \n", l2cap_buf[9]);
                        DPRINTF("\t Reason = 0x%04X \n", (l2cap_buf[12] | (l2cap_buf[13] << 8)));
                        DPRINTF("\t DATA = 0x%04X \n", (l2cap_buf[14] | (l2cap_buf[15] << 8)));
                        break;

                    case L2CAP_CMD_CONNECTION_REQUEST:
                        DPRINTF("Connection Request ID = 0x%02X \n", l2cap_buf[9]);
                        DPRINTF("\t PSM = 0x%04X \n", (l2cap_buf[12] | (l2cap_buf[13] << 8)));
                        DPRINTF("\t SCID = 0x%04X \n", (l2cap_buf[14] | (l2cap_buf[15] << 8)));

                        if ((l2cap_buf[12] | (l2cap_buf[13] << 8)) == L2CAP_PSM_CTRL) {
                            ds34pad[pad].control_scid = l2cap_buf[14] | (l2cap_buf[15] << 8);
                            l2cap_connection_response(ds34pad[pad].hci_handle, l2cap_buf[9], control_dcid, ds34pad[pad].control_scid, PENDING);
                            DelayThread(CMD_DELAY);
                            l2cap_connection_response(ds34pad[pad].hci_handle, l2cap_buf[9], control_dcid, ds34pad[pad].control_scid, SUCCESSFUL);
                            DelayThread(CMD_DELAY);
                            identifier++;
                            l2cap_config_request(ds34pad[pad].hci_handle, identifier, ds34pad[pad].control_scid);
                        } else if ((l2cap_buf[12] | (l2cap_buf[13] << 8)) == L2CAP_PSM_INTR) {
                            ds34pad[pad].interrupt_scid = l2cap_buf[14] | (l2cap_buf[15] << 8);
                            l2cap_connection_response(ds34pad[pad].hci_handle, l2cap_buf[9], interrupt_dcid, ds34pad[pad].interrupt_scid, PENDING);
                            DelayThread(CMD_DELAY);
                            l2cap_connection_response(ds34pad[pad].hci_handle, l2cap_buf[9], interrupt_dcid, ds34pad[pad].interrupt_scid, SUCCESSFUL);
                            DelayThread(CMD_DELAY);
                            identifier++;
                            l2cap_config_request(ds34pad[pad].hci_handle, identifier, ds34pad[pad].interrupt_scid);
                        }
                        break;

                    case L2CAP_CMD_CONNECTION_RESPONSE:
                        DPRINTF("Connection Response ID = 0x%02X \n", l2cap_buf[9]);
                        DPRINTF("\t PSM = 0x%04X \n", (l2cap_buf[12] | (l2cap_buf[13] << 8)));
                        DPRINTF("\t SCID = 0x%04X \n", (l2cap_buf[14] | (l2cap_buf[15] << 8)));
                        DPRINTF("\t RESULT = 0x%04X \n", (l2cap_buf[16] | (l2cap_buf[17] << 8)));

                        if (((l2cap_buf[16] | (l2cap_buf[17] << 8)) == 0x0000) && ((l2cap_buf[18] | (l2cap_buf[19] << 8)) == 0x0000)) {
                            if ((l2cap_buf[14] | (l2cap_buf[15] << 8)) == control_dcid) {
                                ds34pad[pad].control_scid = l2cap_buf[12] | (l2cap_buf[13] << 8);
                                identifier++;
                                l2cap_config_request(ds34pad[pad].hci_handle, identifier, ds34pad[pad].control_scid);
                            } else if ((l2cap_buf[14] | (l2cap_buf[15] << 8)) == interrupt_dcid) {
                                ds34pad[pad].interrupt_scid = l2cap_buf[12] | (l2cap_buf[13] << 8);
                                identifier++;
                                l2cap_config_request(ds34pad[pad].hci_handle, identifier, ds34pad[pad].interrupt_scid);
                            }
                        }
                        break;

                    case L2CAP_CMD_CONFIG_REQUEST:
                        DPRINTF("Configuration Request ID = 0x%02X \n", l2cap_buf[9]);
                        DPRINTF("\t LEN = 0x%04X \n", (l2cap_buf[10] | (l2cap_buf[11] << 8)));
                        DPRINTF("\t SCID = 0x%04X \n", (l2cap_buf[12] | (l2cap_buf[13] << 8)));
                        DPRINTF("\t FLAG = 0x%04X \n", (l2cap_buf[14] | (l2cap_buf[15] << 8)));

                        if ((l2cap_buf[12] | (l2cap_buf[13] << 8)) == control_dcid) {
                            l2cap_config_response(ds34pad[pad].hci_handle, l2cap_buf[9], ds34pad[pad].control_scid);
                        } else if ((l2cap_buf[12] | (l2cap_buf[13] << 8)) == interrupt_dcid) {
                            l2cap_config_response(ds34pad[pad].hci_handle, l2cap_buf[9], ds34pad[pad].interrupt_scid);
                        }
                        break;

                    case L2CAP_CMD_CONFIG_RESPONSE:
                        DPRINTF("Configuration Response ID = 0x%02X \n", l2cap_buf[9]);
                        DPRINTF("\t LEN = 0x%04X \n", (l2cap_buf[10] | (l2cap_buf[11] << 8)));
                        DPRINTF("\t SCID = 0x%04X \n", (l2cap_buf[12] | (l2cap_buf[13] << 8)));
                        DPRINTF("\t FLAG = 0x%04X \n", (l2cap_buf[14] | (l2cap_buf[15] << 8)));
                        DPRINTF("\t RESULT = 0x%04X \n", (l2cap_buf[16] | (l2cap_buf[17] << 8)));

                        if ((l2cap_buf[12] | (l2cap_buf[13] << 8)) == control_dcid) {
                            if (ds34pad[pad].type == DS4 || ds34pad[pad].type == DUALSENSE) {
                                identifier++;
                                l2cap_connection_request(ds34pad[pad].hci_handle, identifier, interrupt_dcid, L2CAP_PSM_INTR);
                            }
                        } else if ((l2cap_buf[12] | (l2cap_buf[13] << 8)) == interrupt_dcid) {
                            hid_initDS34(pad);
                            if (ds34pad[pad].type == DS3) {
                                ds34pad[pad].oldled[0] = led_patterns[pad][1];
                                ds34pad[pad].oldled[3] = 0;
                            } else if ((ds34pad[pad].type == DS4 || ds34pad[pad].type == DUALSENSE) && !ds34pad[pad].sony_generation_pending) {
                                ds34pad[pad].oldled[0] = rgbled_patterns[pad][1][0];
                                ds34pad[pad].oldled[1] = rgbled_patterns[pad][1][1];
                                ds34pad[pad].oldled[2] = rgbled_patterns[pad][1][2];
                                ds34pad[pad].oldled[3] = 0;
                            }
                            DelayThread(CMD_DELAY);
                            if (!ds34pad[pad].sony_generation_pending)
                                hid_LEDRumbleCommand(ds34pad[pad].oldled, 0, 0, pad);
                            pad_status_set(DS34BT_STATE_RUNNING, pad);
                            wireless_mark_connected(ds34pad[pad].bdaddr, 1);
                            if (pad == g_wireless_direct_pad) g_wireless_direct_pad = -1;
                            pademu_trace_record(TITAN_PADEMU_TRACE_BT_HID_RUNNING, 0, (u32)pad, (u32)ds34pad[pad].type,
                                                (u32)ds34pad[pad].hci_handle);
                        }
                        ds34pad[pad].btn_delay = 0xFF;
                        break;

                    case L2CAP_CMD_DISCONNECT_REQUEST:
                        DPRINTF("Disconnect Request SCID = 0x%04X \n", (l2cap_buf[12] | (l2cap_buf[13] << 8)));

                        if ((l2cap_buf[12] | (l2cap_buf[13] << 8)) == control_dcid) {
                            pad_status_set(DS34BT_STATE_DISCONNECTING, pad);
                            l2cap_disconnection_response(ds34pad[pad].hci_handle, l2cap_buf[9], control_dcid, ds34pad[pad].control_scid);
                        } else if ((l2cap_buf[12] | (l2cap_buf[13] << 8)) == interrupt_dcid) {
                            pad_status_set(DS34BT_STATE_DISCONNECTING, pad);
                            l2cap_disconnection_response(ds34pad[pad].hci_handle, l2cap_buf[9], interrupt_dcid, ds34pad[pad].interrupt_scid);
                        }
                        break;

                    case L2CAP_CMD_DISCONNECT_RESPONSE:
                        DPRINTF("Disconnect Response SCID = 0x%04X \n", (l2cap_buf[12] | (l2cap_buf[13] << 8)));

                        if ((l2cap_buf[12] | (l2cap_buf[13] << 8)) == ds34pad[pad].control_scid) {
                            pad_status_set(DS34BT_STATE_DISCONNECTING, pad);
                            hci_disconnect(ds34pad[pad].hci_handle);
                        } else if ((l2cap_buf[12] | (l2cap_buf[13] << 8)) == ds34pad[pad].interrupt_scid) {
                            pad_status_set(DS34BT_STATE_DISCONNECTING, pad);
                            identifier++;
                            l2cap_disconnection_request(ds34pad[pad].hci_handle, identifier, ds34pad[pad].control_scid, control_dcid);
                        }
                        break;

                    default:
                        break;
                }
            } else if (l2cap_interrupt_channel) {
                hid_readReport(l2cap_buf, bytes, pad);
            } else if (l2cap_command_channel) {
                DPRINTF("HID command status 0x%02X \n", l2cap_buf[8]);
                pad = MAX_PADS;
            }
        } // acl_handle_ok
    }     // !rcode

    return pad;
}

/* P10.5.7 HCI ACL fragmentation repair: PB=01 continuation. */
static void p1057_acl_reassembly_reset(void)
{
    g_p1057_acl_reassembly_used = 0;
    g_p1057_acl_reassembly_expected = 0;
    g_p1057_acl_reassembly_handle = 0;
    g_p1057_acl_reassembly_active = 0;
}

/* Return 1 when l2cap_buf contains one complete HCI ACL/L2CAP PDU and
 * *out_bytes is its contiguous size. Return 0 when the callback contained
 * only an incomplete/orphaned fragment and must not reach L2CAP_event_task. */
static int p1057_acl_prepare(int resultCode, int bytes, int *out_bytes)
{
    u16 handle_flags;
    u16 handle;
    u16 acl_payload_len;
    u16 l2cap_payload_len;
    u16 expected;
    u16 packet_bytes;
    u16 copy_len;
    u8 pb;

    if (out_bytes)
        *out_bytes = bytes;

    if (resultCode != USB_RC_OK || bytes < 4) {
        p1057_acl_reassembly_reset();
        return 1;
    }

    handle_flags = (u16)(l2cap_buf[0] | ((u16)l2cap_buf[1] << 8));
    handle = (u16)(handle_flags & 0x0FFFu);
    pb = (u8)((handle_flags >> 12) & 0x03u);
    acl_payload_len = (u16)(l2cap_buf[2] | ((u16)l2cap_buf[3] << 8));

    if (acl_payload_len > (u16)(bytes - 4)) {
        g_p1057_acl_reassembly_errors++;
        if (g_p1057_acl_reassembly_errors <= 4u ||
            (g_p1057_acl_reassembly_errors & 255u) == 0u)
            pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, -1,
                                TITAN_WIRELESS_BA(23), (u32)bytes,
                                ((u32)acl_payload_len << 16) | (u32)pb);
        p1057_acl_reassembly_reset();
        return 0;
    }

    /* Bluetooth Core HCI ACL PB=01: continuing fragment. It has an HCI
     * header, but its payload starts where the previous L2CAP PDU stopped. */
    if (pb == 0x01u) {
        if (!g_p1057_acl_reassembly_active ||
            handle != g_p1057_acl_reassembly_handle) {
            g_p1057_acl_reassembly_errors++;
            if (g_p1057_acl_reassembly_errors <= 4u ||
                (g_p1057_acl_reassembly_errors & 255u) == 0u)
                pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, -2,
                                    TITAN_WIRELESS_BA(23), (u32)handle,
                                    (u32)g_p1057_acl_reassembly_handle);
            p1057_acl_reassembly_reset();
            return 0;
        }

        copy_len = acl_payload_len;
        if ((u32)g_p1057_acl_reassembly_used + (u32)copy_len >
                (u32)g_p1057_acl_reassembly_expected ||
            (u32)g_p1057_acl_reassembly_used + (u32)copy_len >
                (u32)P1057_ACL_REASSEMBLY_CAP) {
            g_p1057_acl_reassembly_errors++;
            pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, -3,
                                TITAN_WIRELESS_BA(23),
                                (u32)g_p1057_acl_reassembly_used,
                                ((u32)copy_len << 16) |
                                (u32)g_p1057_acl_reassembly_expected);
            p1057_acl_reassembly_reset();
            return 0;
        }

        /* Strip the continuation HCI header; do not invent another L2CAP hdr. */
        mips_memcpy(&g_p1057_acl_reassembly[g_p1057_acl_reassembly_used],
                    &l2cap_buf[4], copy_len);
        g_p1057_acl_reassembly_used =
            (u16)(g_p1057_acl_reassembly_used + copy_len);

        if (g_p1057_acl_reassembly_used < g_p1057_acl_reassembly_expected)
            return 0;

        /* Make the synthetic HCI ACL header describe the complete L2CAP PDU. */
        g_p1057_acl_reassembly[2] =
            (u8)((g_p1057_acl_reassembly_expected - 4u) & 0xFFu);
        g_p1057_acl_reassembly[3] =
            (u8)((g_p1057_acl_reassembly_expected - 4u) >> 8);
        mips_memcpy(l2cap_buf, g_p1057_acl_reassembly,
                    g_p1057_acl_reassembly_expected);
        if (out_bytes)
            *out_bytes = (int)g_p1057_acl_reassembly_expected;

        g_p1057_acl_reassembled_packets++;
        if (g_p1057_acl_reassembled_packets <= 4u ||
            (g_p1057_acl_reassembled_packets & 255u) == 0u)
            pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0,
                                TITAN_WIRELESS_BA(22),
                                (u32)g_p1057_acl_reassembly_expected,
                                ((u32)handle << 16) |
                                (u32)g_p1057_acl_reassembled_packets);
        p1057_acl_reassembly_reset();
        return 1;
    }

    /* Start/complete fragment. bytes[4..7] is the one real L2CAP header. */
    p1057_acl_reassembly_reset();
    if (bytes < 8)
        return 1;

    l2cap_payload_len = (u16)(l2cap_buf[4] | ((u16)l2cap_buf[5] << 8));
    expected = (u16)(8u + l2cap_payload_len);
    if (expected <= (u16)bytes)
        return 1;

    if (expected > P1057_ACL_REASSEMBLY_CAP) {
        g_p1057_acl_reassembly_errors++;
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, -4,
                            TITAN_WIRELESS_BA(23), (u32)expected,
                            (u32)P1057_ACL_REASSEMBLY_CAP);
        return 0;
    }

    packet_bytes = (u16)(4u + acl_payload_len);
    if (packet_bytes > (u16)bytes)
        packet_bytes = (u16)bytes;
    if (packet_bytes > expected)
        packet_bytes = expected;

    mips_memcpy(g_p1057_acl_reassembly, l2cap_buf, packet_bytes);
    g_p1057_acl_reassembly_used = packet_bytes;
    g_p1057_acl_reassembly_expected = expected;
    g_p1057_acl_reassembly_handle = handle;
    g_p1057_acl_reassembly_active = 1;
    return 0;
}

static void l2cap_event_cb(int resultCode, int bytes, void *arg)
{
    int ret;
    u16 interrupt_dcid = 0x0041;

    PollSema(bt_dev.hid_sema);

    /* P10.6.5 / P166: observe completed Bulk-IN callbacks, not submit rc. */
    g_p1065_l2cap_cb_count++;
    g_p1065_l2cap_last_result = resultCode;
    g_p1065_l2cap_last_bytes = bytes;
    if (resultCode != USB_RC_OK || bytes <= 0) {
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_L2CAP_RX, resultCode,
                            0x50313636u, /* P166 */
                            (u32)resultCode, (u32)bytes);
    }

    /* P10.5.7: L2CAP parser sees only complete HCI ACL PDUs. */
    {
        int p1057_parse_bytes = bytes;
        if (!p1057_acl_prepare(resultCode, bytes, &p1057_parse_bytes))
            goto p1057_rearm;
        bytes = p1057_parse_bytes;
    }
    ret = L2CAP_event_task(resultCode, bytes);
    /* P10.5.6: successful A1 HID traffic is high-rate; sample it. */
    if (resultCode == USB_RC_OK && bytes > 8 &&
        l2cap_buf[8] == HID_THDR_DATA_INPUT) {
        g_p1056_l2cap_hid_rx_packets++;
    }
    if (resultCode != USB_RC_OK || bytes <= 8 ||
        l2cap_buf[8] != HID_THDR_DATA_INPUT ||
        g_p1056_l2cap_hid_rx_packets <= 4u ||
        (g_p1056_l2cap_hid_rx_packets & 255u) == 0u) {
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_L2CAP_RX, resultCode,
                            (u32)bytes, (u32)ret,
                            (u32)((bytes > 8) ? l2cap_buf[8] : 0));
    }
    if (resultCode == USB_RC_OK && bytes >= 4 &&
        g_wireless_l2cap_watch_pad >= 0 && g_wireless_l2cap_watch_pad < MAX_PADS) {
        u16 rx_handle = (u16)(l2cap_buf[0] | ((l2cap_buf[1] & 0x0F) << 8));
        if (rx_handle == ds34pad[g_wireless_l2cap_watch_pad].hci_handle) {
            g_wireless_l2cap_watch_pad = -1;
            g_wireless_l2cap_watch_queries = 0u;
            g_wireless_l2cap_retry_count = 0u;
        }
    }

    if (ret < MAX_PADS) {
        if (pad_status_check(DS34BT_STATE_RUNNING, ret)) {
            if (pad_status_check(DS34BT_STATE_DISCONNECT_REQUEST, ret)) {
                if (!ds34pad[ret].isfake) {
                    if (ds34pad[ret].type == DS4 || ds34pad[ret].type == DUALSENSE) {
                        interrupt_dcid = 0x0071;
                    }
                    identifier++;
                    l2cap_disconnection_request(ds34pad[ret].hci_handle, identifier, ds34pad[ret].interrupt_scid, interrupt_dcid);
                } else {
                    hci_disconnect(ds34pad[ret].hci_handle);
                }
                pad_status_clear(DS34BT_STATE_DISCONNECT_REQUEST, ret);
            } else if (ds34pad[ret].update_rum && ds34pad[ret].type != DUALSENSE) {
                hid_LEDRumbleCommand(ds34pad[ret].oldled, ds34pad[ret].lrum, ds34pad[ret].rrum, ret);
                ds34pad[ret].update_rum = 0;
            }
        } else {
            if (!ds34pad[ret].isfake && ds34pad[ret].type == DS3)
                DelayThread(42000); // fix for some bt adapters
        }
    }

p1057_rearm:
    (void)p1064_l2cap_rx_arm_locked(arg, 0u);
    SignalSema(bt_dev.hid_sema);
}

/************************************************************/
/* HID Commands                                             */
/************************************************************/

static int HID_command(u16 handle, u16 scid, u8 *data, u8 length, int pad)
{
    l2cap_cmd_buf[0] = (u8)(handle & 0xff); // HCI handle with PB,BC flag
    l2cap_cmd_buf[1] = (u8)(((handle >> 8) & 0x0f) | 0x20);
    l2cap_cmd_buf[2] = (u8)((4 + length) & 0xff); // HCI ACL total data length
    l2cap_cmd_buf[3] = (u8)((4 + length) >> 8);
    l2cap_cmd_buf[4] = (u8)(length & 0xff); // L2CAP header: Length
    l2cap_cmd_buf[5] = (u8)(length >> 8);
    l2cap_cmd_buf[6] = (u8)(scid & 0xff); // L2CAP header: Channel ID
    l2cap_cmd_buf[7] = (u8)(scid >> 8);

    mips_memcpy(&l2cap_cmd_buf[8], data, length);

    // output on endpoint 2
    return UsbBulkTransfer(bt_dev.outEndp, l2cap_cmd_buf, (8 + length), NULL, NULL);
}

static int hid_initDS34(int pad)
{
    u8 init_buf[PS3_F4_REPORT_LEN + 2];
    u8 size = 2;

    if (ds34pad[pad].sony_generation_pending)
        return 0;

    if (ds34pad[pad].type == DS3) {
        init_buf[0] = HID_THDR_SET_REPORT_FEATURE; // THdr
        init_buf[1] = PS3_F4_REPORT_ID;            // Report ID
        init_buf[2] = 0x42;
        init_buf[3] = 0x03;
        init_buf[4] = 0x00;
        init_buf[5] = 0x00;
        size += PS3_F4_REPORT_LEN;
    } else if (ds34pad[pad].type == DS4) {
        init_buf[0] = HID_THDR_GET_REPORT_FEATURE; // THdr
        init_buf[1] = PS4_02_REPORT_ID;            // Report ID
    }

    return HID_command(ds34pad[pad].hci_handle, ds34pad[pad].control_scid, init_buf, size, pad);
}

/**
 * Send a HID command with LED and rumble status
 * @param led 4 bytes describing LED state.
 * For DS3, first byte is bitfield of LEDs switched on - (1 << n) for LED indicator number n (1 ~ 4)
 * For DS4, first three bytes are R, G, B values of the front light
 * Fourth byte indicates whether light should be blinking (1) or solid (0).
 * @param lrum Strength of left rumble motor
 * @param lrum Strength of right rumble motor
 * @param pad Which pad the command should be sent to
 */
static int hid_LEDRumbleCommand(u8 *led, u8 lrum, u8 rrum, int pad)
{
    u8 led_buf[PS4_11_REPORT_LEN + 2];
    u8 size = 2;
    u8 led_bit, i;

    if (ds34pad[pad].type == DS3) {
        led_buf[0] = HID_THDR_SET_REPORT_OUTPUT; // THdr
        led_buf[1] = PS3_01_REPORT_ID;           // Report ID

        u8 *command = &led_buf[size];
        mips_memset(command, 0, OUTPUT_01_REPORT_SIZE);

        if (ds34pad[pad].isfake) {
            if (rrum < 5) {
                rrum = 0;
            }
        }

        command[1] = 0xFE;          // rt
        command[2] = rrum;          // rp
        command[3] = 0xFE;          // lt
        command[4] = lrum;          // lp
        command[9] = led[0] & 0x1F; // LED Conf
        for (led_bit = 0x10, i = 0; i < 4; i++, led_bit >>= 1) {
            if (led[0] & led_bit) {
                mips_memcpy(&command[10 + (i * 5)], hid_cmd_payload_led_arguments, sizeof(hid_cmd_payload_led_arguments));
                if (led[3]) {
                    command[10 + (i * 5) + 3] = 0x32;
                }
            }
        }

        size += OUTPUT_01_REPORT_SIZE;
    } else if (ds34pad[pad].type == DS4) {
        mips_memset(led_buf, 0, PS3_01_REPORT_LEN + 2);

        led_buf[0] = HID_THDR_SET_REPORT_OUTPUT; // THdr
        led_buf[1] = PS4_11_REPORT_ID;           // Report ID
        led_buf[2] = 0x80;                       // update rate 1000Hz
        led_buf[4] = 0xFF;

        led_buf[7] = rrum * 255;
        led_buf[8] = lrum;

        led_buf[9] = led[0];  // r
        led_buf[10] = led[1]; // g
        led_buf[11] = led[2]; // b

        if (led[3]) {           // means charging, so blink
            led_buf[12] = 0x80; // Time to flash bright (255 = 2.5 seconds)
            led_buf[13] = 0x80; // Time to flash dark (255 = 2.5 seconds)
        }

        size += PS4_11_REPORT_LEN;
    }

    ds34pad[pad].oldled[0] = led[0];
    ds34pad[pad].oldled[1] = led[1];
    ds34pad[pad].oldled[2] = led[2];
    ds34pad[pad].oldled[3] = led[3];

    return HID_command(ds34pad[pad].hci_handle, ds34pad[pad].control_scid, led_buf, size, pad);
}

static void hid_readReport(u8 *data, int bytes, int pad_idx)
{
    ds34bt_pad_t *pad = &ds34pad[pad_idx];
    if (data[8] == HID_THDR_DATA_INPUT) {
        /* P10.5.6 Sony basic BT report 0x01: 10-byte gamepad payload. */
        if (data[9] == 0x01 && pad->sony_generation_pending) {
            struct ds4report basic;
            int feature_rc = 0;

            if (bytes < 19)
                return;

            /* data[9..18] is exactly report-id + 9-byte basic Sony payload.
             * Zero the tail so translate_pad_ds4 never observes stale memory. */
            mips_memset(&basic, 0, sizeof(basic));
            mips_memcpy(&basic, &data[9], 10);

            translate_pad_ds4(&basic, &pad->ds2, 0);
            pad->system_button = basic.PSButton ? 1 : 0;
            pademu_telemetry_system_button((unsigned)pad_idx, pad->type,
                                           pad->system_button);
            pad->battery_state = TITAN_PADEMU_BATTERY_UNKNOWN;
            pad->battery_percent = 0;
            pad->battery_valid = 0;
            pad->battery_raw = 0;
            pad->telemetry_packets++;
            if (pad->telemetry_packets <= 4u ||
                (pad->telemetry_packets & 63u) == 0u)
                pademu_trace_record(TITAN_PADEMU_TRACE_BT_HID_INPUT, 0,
                                    (u32)pad_idx, (u32)pad->type,
                                    pad->telemetry_packets);
            padMacroPerform(&pad->ds2, basic.PSButton);
            pad->update_rum = 0;

            if (pad->telemetry_packets == 1u)
                pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0,
                                    TITAN_WIRELESS_BA(20), (u32)bytes,
                                    ((u32)data[8] << 8) | (u32)data[9]);

            if (!g_p1056_sony_feature05_requested[pad_idx]) {
                /* Mark before submitting so a callback/re-entry cannot spam it. */
                g_p1056_sony_feature05_requested[pad_idx] = 1;
                feature_rc = hid_request_sony_bt_feature05(pad_idx);
                pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING,
                                    feature_rc, TITAN_WIRELESS_BA(21),
                                    (u32)pad_idx, 0x00000005u);
            }
        } else if (data[9] == PS3_01_REPORT_ID) {
            if (bytes < 19) {
                // Too small
                return;
            }

            u8 press_emu;
            if (bytes < sizeof(struct ds3report)) {
                // Too small for full report
                press_emu = 1;
            } else {
                press_emu = g_press_emu;
            }

            struct ds3report *report = (struct ds3report *)&data[11];

            if (report->RightStickX == 0 && report->RightStickY == 0) // ledrumble cmd causes null report sometime
                return;


            translate_pad_ds3(report, &pad->ds2, press_emu);
            pad->system_button=report->PSButton?1:0;
            pademu_telemetry_system_button((unsigned)pad_idx,pad->type,pad->system_button);
            if (!press_emu) telemetry_bt_ds3(pad, report->Power); else { pad->battery_state=TITAN_PADEMU_BATTERY_UNKNOWN; pad->battery_valid=0; }
            pad->telemetry_packets++;
            if (pad->telemetry_packets <= 4u || (pad->telemetry_packets & 63u) == 0u)
                pademu_trace_record(TITAN_PADEMU_TRACE_BT_HID_INPUT, 0, (u32)pad_idx, (u32)pad->type, pad->telemetry_packets);
            padMacroPerform(&pad->ds2, report->PSButton);

            u8 reported_power = 0;
            if (press_emu) {
                reported_power = 0x05;
            } else {
                reported_power = report->Power;
            }

            if (report->PSButton) {                                    // display battery level
                if (report->Select && (pad->btn_delay == MAX_DELAY)) { // PS + SELECT
                    if (pad->analog_btn < 2)                           // unlocked mode
                        pad->analog_btn = !pad->analog_btn;

                    pad->oldled[0] = led_patterns[pad_idx][(pad->analog_btn & 1)];
                    pad->btn_delay = 1;
                } else {
                    if (reported_power <= 0x05)
                        pad->oldled[0] = power_level[reported_power];

                    if (pad->btn_delay < MAX_DELAY)
                        pad->btn_delay++;
                }
            } else {
                pad->oldled[0] = led_patterns[pad_idx][(pad->analog_btn & 1)];

                if (pad->btn_delay > 0) {
                    pad->btn_delay--;
                }
            }

            // If charging or about to die
            if (reported_power == 0xEE || reported_power == 0x01) {
                // Blink for the next update frame (but this executes every frame)
                pad->oldled[3] = 1;
            }

        } else if (data[9] == 0x31) {
            struct dualsensereport report;
            u8 *common;
            if (bytes < 87)
                return;
            /* HID payload: THDR, report 0x31, seq/tag, then the 63-byte common report. */
            common = &data[11];
            mips_memset(&report, 0, sizeof(report));
            report.ReportID = 0x01;
            report.LeftStickX = common[0]; report.LeftStickY = common[1];
            report.RightStickX = common[2]; report.RightStickY = common[3];
            report.PressureL2 = common[4]; report.PressureR2 = common[5];
            report.Sequence = common[6];
            *((u8 *)&report + 8) = common[7];
            *((u8 *)&report + 9) = common[8];
            *((u8 *)&report + 10) = common[9];
            report.Status0 = common[52];
            ds34pad[pad_idx].type = DUALSENSE;
            ds34pad[pad_idx].sony_generation_pending = 0;
            { int wi = wireless_accessory_find(ds34pad[pad_idx].bdaddr); if (wi >= 0) { g_wireless.accessories[wi].device_type = TITAN_PADEMU_DEVICE_DUALSENSE; g_wireless.accessories[wi].flags |= TITAN_PADEMU_WIRELESS_CONTROLLER | TITAN_PADEMU_WIRELESS_CONNECTED; wireless_update_bond_metadata(ds34pad[pad_idx].bdaddr, TITAN_PADEMU_DEVICE_DUALSENSE, g_wireless.accessories[wi].name); wireless_state_changed(); } }
            translate_pad_dualsense(&report, &pad->ds2);
            pad->system_button=report.PSButton?1:0;
            pademu_telemetry_system_button((unsigned)pad_idx,pad->type,pad->system_button); telemetry_bt_dualsense(pad, report.Status0); pad->telemetry_packets++;
            if (pad->telemetry_packets <= 4u || (pad->telemetry_packets & 63u) == 0u) pademu_trace_record(TITAN_PADEMU_TRACE_BT_HID_INPUT,0,(u32)pad_idx,(u32)pad->type,pad->telemetry_packets);
            padMacroPerform(&pad->ds2, report.PSButton);
            pad->update_rum = 0;
        } else if (data[9] == PS4_11_REPORT_ID) {
            ds34pad[pad_idx].type = DS4;
            ds34pad[pad_idx].sony_generation_pending = 0;
            { int wi = wireless_accessory_find(ds34pad[pad_idx].bdaddr); if (wi >= 0) { g_wireless.accessories[wi].device_type = TITAN_PADEMU_DEVICE_DS4; g_wireless.accessories[wi].flags |= TITAN_PADEMU_WIRELESS_CONTROLLER | TITAN_PADEMU_WIRELESS_CONNECTED; wireless_update_bond_metadata(ds34pad[pad_idx].bdaddr, TITAN_PADEMU_DEVICE_DS4, g_wireless.accessories[wi].name); wireless_state_changed(); } }
            struct ds4report *report = (struct ds4report *)&data[11];
            translate_pad_ds4(report, &pad->ds2, bytes > 63);
            pad->system_button=report->PSButton?1:0;
            pademu_telemetry_system_button((unsigned)pad_idx,pad->type,pad->system_button); telemetry_bt_ds4(pad, report); pad->telemetry_packets++;
            if (pad->telemetry_packets <= 4u || (pad->telemetry_packets & 63u) == 0u) pademu_trace_record(TITAN_PADEMU_TRACE_BT_HID_INPUT,0,(u32)pad_idx,(u32)pad->type,pad->telemetry_packets);
            padMacroPerform(&pad->ds2, report->PSButton);

            if (report->PSButton) {                                   // display battery level
                if (report->Share && (pad->btn_delay == MAX_DELAY)) { // PS + Share
                    if (pad->analog_btn < 2) {                        // unlocked mode
                        pad->analog_btn = !pad->analog_btn;
                    }

                    pad->oldled[0] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][0];
                    pad->oldled[1] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][1];
                    pad->oldled[2] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][2];
                    pad->btn_delay = 1;
                } else {
                    pad->oldled[0] = report->Battery;
                    pad->oldled[1] = 0;
                    pad->oldled[2] = 0;

                    if (pad->btn_delay < MAX_DELAY) {
                        pad->btn_delay++;
                    }
                }
            } else {
                pad->oldled[0] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][0];
                pad->oldled[1] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][1];
                pad->oldled[2] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][2];

                if (pad->btn_delay > 0) {
                    pad->btn_delay--;
                }
            }

            if (report->Power != 0xB && report->Usb_plugged) { // charging
                pad->oldled[3] = 1;
            }
        }

        if (pad->btn_delay > 0) {
            pad->update_rum = 1;
        }
    } else {
        DPRINTF("Unmanaged Input Report: THDR 0x%02X ", data[8]);
        DPRINTF(" ID 0x%02X \n", data[9]);
    }
}

/************************************************************/
/* DS34BT Commands                                          */
/************************************************************/

void ds34bt_set_rumble(u8 lrum, u8 rrum, int port)
{
    WaitSema(bt_dev.hid_sema);

    ds34pad[port].update_rum = 1;
    ds34pad[port].lrum = lrum;
    ds34pad[port].rrum = rrum;

    SignalSema(bt_dev.hid_sema);
}

int ds34bt_get_data(u8 *dst, int size, int port)
{
    int ret;

    WaitSema(bt_dev.hid_sema);
    p1064_l2cap_rx_recover_if_needed_locked(2u);

    mips_memcpy(dst, ds34pad[port].data, size);
    ret = ds34pad[port].analog_btn & 1;

    SignalSema(bt_dev.hid_sema);

    return ret;
}

void ds34bt_set_mode(int mode, int lock, int port)
{
    WaitSema(bt_dev.hid_sema);

    if (lock == 3)
        ds34pad[port].analog_btn = 3;
    else
        ds34pad[port].analog_btn = mode;

    SignalSema(bt_dev.hid_sema);
}

int ds34bt_get_status(int port)
{
    int ret;

    WaitSema(bt_dev.hid_sema);
    p1064_l2cap_rx_recover_if_needed_locked(3u);

    ret = ds34pad[port].status;

    SignalSema(bt_dev.hid_sema);

    return ret;
}

int ds34bt_init(u8 pads, u8 options)
{
    int ret, i, chrg_rc;
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_INIT_BEGIN, 0, (u32)pads, (u32)options, 0u);
    xbox_ble_init_transport();

    mips_memset(&g_bt_transport, 0, sizeof(g_bt_transport));
    g_bt_transport.usb_dev_id = -1;
    g_bt_transport.stage = TITAN_PADEMU_BT_STAGE_IDLE;

    mips_memset(&g_wireless, 0, sizeof(g_wireless));
    g_wireless.version = TITAN_PADEMU_WIRELESS_VERSION;
    g_wireless.selected = TITAN_WIRELESS_INVALID_INDEX;
    g_wireless_direct_pad = -1;
    g_wireless_name_index = -1;
    g_wireless_scan_pending = 0;
    g_wireless_scan_requested = 0;
    g_wireless_hci_startup_ready = 0;
    g_wireless_ssp_event_mask = 0;
    g_wireless_ssp_mode = 0;
    g_wireless_wait_ble_stop = 0;
    g_wireless_l2cap_watch_pad = -1;
    g_wireless_l2cap_watch_queries = 0u;
    g_wireless_l2cap_retry_count = 0u;
    g_classic_bonds_loaded = 0;
    g_classic_bonds_dirty = 0;
    wireless_load_bonds();

    for (i = 0; i < MAX_PADS; i++)
        ds34pad[i].enabled = (pads >> i) & 1;

    ds34pad_init();

    enable_fake = options & 1;

    bt_dev.hid_sema = CreateMutex(IOP_MUTEX_UNLOCKED);
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_MUTEX_CREATE, bt_dev.hid_sema, (u32)pads, (u32)options, 0u);

    if (bt_dev.hid_sema < 0) {
        DPRINTF("DS34BT: Failed to allocate semaphore.\n");
        return 0;
    }

    ret = UsbRegisterDriver(&bt_driver);
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_DRIVER_REGISTER, ret, (u32)pads, (u32)options, 0u);

    if (ret != USB_RC_OK) {
        DPRINTF("DS34BT: Error registering USB devices: %02X\n", ret);
        bt_diag_stage(TITAN_PADEMU_BT_STAGE_DRIVER_REGISTER_FAILED, ret);
        return 0;
    }
    bt_diag_stage(TITAN_PADEMU_BT_STAGE_DRIVER_REGISTERED, 0);

    chrg_rc = UsbRegisterDriver(&chrg_driver);
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_CHARGE_DRIVER, chrg_rc, 0u, 0u, 0u);

    return 1;
}

static void wireless_refresh_live_state(void)
{
    unsigned i;
    int pad;
    /* Preserve SEEN/PAIRED and rebuild only volatile connection bits. */
    for (i = 0; i < g_wireless.count; i++)
        g_wireless.accessories[i].flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTED;

    for (pad = 0; pad < MAX_PADS; pad++) {
        int wi;
        if (!pad_status_check(DS34BT_STATE_CONNECTED, pad) && !pad_status_check(DS34BT_STATE_RUNNING, pad)) continue;
        wi = wireless_accessory_get_or_add(ds34pad[pad].bdaddr);
        if (wi < 0) continue;
        g_wireless.accessories[wi].flags |= TITAN_PADEMU_WIRELESS_CONTROLLER;
        if (pad_status_check(DS34BT_STATE_RUNNING, pad)) {
            g_wireless.accessories[wi].flags |= TITAN_PADEMU_WIRELESS_CONNECTED;
            g_wireless.accessories[wi].flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTING;
        } else {
            g_wireless.accessories[wi].flags |= TITAN_PADEMU_WIRELESS_CONNECTING;
            g_wireless.accessories[wi].flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTED;
        }
        if (ds34pad[pad].type == DUALSENSE) g_wireless.accessories[wi].device_type = TITAN_PADEMU_DEVICE_DUALSENSE;
        else if (ds34pad[pad].type == DS4) g_wireless.accessories[wi].device_type = TITAN_PADEMU_DEVICE_DS4;
        else if (ds34pad[pad].type == DS3) g_wireless.accessories[wi].device_type = TITAN_PADEMU_DEVICE_DS3;
        else if (ds34pad[pad].type == XBOX_SERIES) g_wireless.accessories[wi].device_type = TITAN_PADEMU_DEVICE_XBOX_SERIES;
        if (ds34pad[pad].battery_valid) g_wireless.accessories[wi].battery_percent = ds34pad[pad].battery_percent;
    }

    /* Surface the already-existing Xbox BLE backend through the same system UI
     * without changing its HOGP/SMP transport behavior. */
    if (xbox_ble_bond.magic == TITAN_BLE_BOND_VALID || xbox_ble.peer[0] || xbox_ble.peer[1] || xbox_ble.peer[2] || xbox_ble.peer[3] || xbox_ble.peer[4] || xbox_ble.peer[5]) {
        const u8 *addr = (xbox_ble.peer[0] || xbox_ble.peer[1] || xbox_ble.peer[2] || xbox_ble.peer[3] || xbox_ble.peer[4] || xbox_ble.peer[5]) ? xbox_ble.peer : xbox_ble_bond.addr;
        int wi = wireless_accessory_get_or_add(addr);
        if (wi >= 0) {
            TitanPademuWirelessAccessory *a = &g_wireless.accessories[wi];
            a->flags |= TITAN_PADEMU_WIRELESS_BLE | TITAN_PADEMU_WIRELESS_CONTROLLER;
            if (xbox_ble_bond.magic == TITAN_BLE_BOND_VALID) a->flags |= TITAN_PADEMU_WIRELESS_PAIRED;
            if (xbox_ble.state == TITAN_BLE_READY) a->flags |= TITAN_PADEMU_WIRELESS_CONNECTED;
            if (xbox_ble.state == TITAN_BLE_CONNECTING || xbox_ble.state == TITAN_BLE_PAIRING || xbox_ble.state == TITAN_BLE_ENCRYPTING) a->flags |= TITAN_PADEMU_WIRELESS_CONNECTING;
            a->device_type = TITAN_PADEMU_DEVICE_XBOX_SERIES;
            wireless_copy_name(a->name, sizeof(a->name), "Xbox Wireless Controller");
        }
    }

    if (bt_dev.status & DS34BT_STATE_USB_CONFIGURED) g_wireless.flags |= TITAN_PADEMU_WIRELESS_TRANSPORT_READY;
    else g_wireless.flags &= ~TITAN_PADEMU_WIRELESS_TRANSPORT_READY;
    if (g_wireless_direct_pad >= 0) g_wireless.flags |= TITAN_PADEMU_WIRELESS_CONNECT_IN_PROGRESS;
    else g_wireless.flags &= ~TITAN_PADEMU_WIRELESS_CONNECT_IN_PROGRESS;
    g_wireless.last_hci_event = g_bt_transport.last_hci_event;
    g_wireless.last_hci_opcode = g_bt_transport.last_hci_opcode;
}

int ds34bt_wireless_command(const TitanPademuWirelessRequest *request, TitanPademuWirelessState *state)
{
    u32 command = TITAN_PADEMU_WIRELESS_QUERY;
    u32 index = TITAN_WIRELESS_INVALID_INDEX;
    int rc = 1;

    wireless_load_bonds();
    /* Dirty bonds are deliberately written from this RPC thread, never from
     * the USB/HCI callback that delivered Link Key Notification. */
    wireless_flush_bonds();
    wireless_refresh_live_state();

    if (request && request->version == TITAN_PADEMU_WIRELESS_VERSION) {
        command = request->command;
        index = request->index;
    }

    /* QUERY is already throttled to ~4 Hz by the EE bridge.  Use it as a
     * low-cost watchdog for exactly one missing first L2CAP response.  This
     * is not a generic retry loop: one retry maximum, only while the ACL is
     * connected, HID is not RUNNING, and no L2CAP RX has been observed. */
    if (command == TITAN_PADEMU_WIRELESS_QUERY &&
        g_wireless_l2cap_watch_pad >= 0 && g_wireless_l2cap_watch_pad < MAX_PADS &&
        g_wireless_direct_pad == g_wireless_l2cap_watch_pad &&
        pad_status_check(DS34BT_STATE_CONNECTED, g_wireless_l2cap_watch_pad) &&
        !pad_status_check(DS34BT_STATE_RUNNING, g_wireless_l2cap_watch_pad) &&
        ds34pad[g_wireless_l2cap_watch_pad].control_scid == 0) {
        g_wireless_l2cap_watch_queries++;
        if (g_wireless_l2cap_watch_queries >= 8u && g_wireless_l2cap_retry_count == 0u) {
            int retry_rc;
            int retry_pad = g_wireless_l2cap_watch_pad;
            g_wireless_l2cap_watch_queries = 0u;
            g_wireless_l2cap_retry_count = 1u;
            identifier++;
            retry_rc = l2cap_connection_request(ds34pad[retry_pad].hci_handle, identifier,
                                                0x0070, L2CAP_PSM_CTRL);
            pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, retry_rc,
                                TITAN_WIRELESS_BA(17), (u32)retry_pad, 1u);
        }
    }

    if (command == TITAN_PADEMU_WIRELESS_BEGIN_SCAN) {
        unsigned i;
        for (i = 0; i < g_wireless.count; i++)
            g_wireless.accessories[i].flags &= (u8)~TITAN_PADEMU_WIRELESS_SEEN;
        g_wireless.last_error = 0;
        g_wireless.flags &= ~TITAN_PADEMU_WIRELESS_HAS_ERROR;
        g_wireless.flags |= TITAN_PADEMU_WIRELESS_SCANNING;
        g_wireless.flags &= ~TITAN_PADEMU_WIRELESS_SSP_READY;
        g_wireless_scan_requested = 1;
        g_wireless_bootstrap_scan_pending = 0;

        /* P10.6.2: P10.6.1 can finish Read_BD_ADDR during Health & Safety
         * before Settings ever requests discovery. In that late-BEGIN_SCAN
         * case there is no later HCI callback left to publish Classic
         * readiness. Recover it from the already-proven transport state.
         * This branch is reachable only from explicit BEGIN_SCAN; BOOTSTRAP
         * remains init/state-only and never starts Inquiry by itself. */
        if (bt_dev.controlEndp >= 0 &&
            (bt_dev.status & DS34BT_STATE_USB_CONFIGURED) &&
            sony_host_bdaddr_valid &&
            !g_wireless_hci_startup_ready) {
            g_wireless_hci_startup_ready = 1;
            pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_DISCOVERY, 0,
                                0x50313632u, (u32)bt_dev.status,
                                (u32)sony_host_bdaddr_valid); /* "P162" */
        }

        g_wireless_scan_pending = 0;
        g_wireless_ssp_event_mask = 0;
        g_wireless_ssp_mode = 0;
        g_wireless_name_index = -1;
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_UI, 0, TITAN_WIRELESS_BA(0),
                            (u32)sony_host_bdaddr_valid, (u32)g_wireless_hci_startup_ready);
        if (bt_dev.controlEndp < 0) {
            /* The Pair-on-Demand driver has just been registered; USB probe and
             * configuration are asynchronous. Keep the request queued instead
             * of producing a false user-visible failure. */
            pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_DISCOVERY, 0, TITAN_WIRELESS_BA(1), 0u, 0u);
        } else if (sony_host_bdaddr_valid && g_wireless_hci_startup_ready) {
            if (!wireless_start_classic_scan_sequence()) rc = 0;
        }
        wireless_state_changed();
    } else if (command == TITAN_PADEMU_WIRELESS_CONNECT) {
        if (index >= g_wireless.count) { g_wireless.last_error = -703; rc = 0; }
        else {
            TitanPademuWirelessAccessory *a = &g_wireless.accessories[index];
            int pad;
            if (!(a->flags & (TITAN_PADEMU_WIRELESS_SEEN | TITAN_PADEMU_WIRELESS_PAIRED))) { g_wireless.last_error = -704; rc = 0; }
            else if ((a->flags & TITAN_PADEMU_WIRELESS_BLE) != 0u) {
                /* Xbox HOGP already owns BLE pairing/reconnect. A discovered
                 * Xbox is routed automatically to PADEMU when READY. */
                if (xbox_ble.state == TITAN_BLE_OFF || xbox_ble.state == TITAN_BLE_FAILED) rc = TitanBleHogp_StartScan(&xbox_ble);
                else rc = 1;
                if (!rc) g_wireless.last_error = -705;
            } else {
                int has_bond = wireless_bond_find(a->address) >= 0;
                if (xbox_ble.state == TITAN_BLE_CONNECTING || xbox_ble.state == TITAN_BLE_PAIRING ||
                    xbox_ble.state == TITAN_BLE_ENCRYPTING || xbox_ble.state == TITAN_BLE_GATT_SERVICE ||
                    xbox_ble.state == TITAN_BLE_GATT_CHARS || xbox_ble.state == TITAN_BLE_GATT_DESCS) {
                    g_wireless.last_error = -711;
                    g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
                    rc = 0;
                    wireless_state_changed();
                    if (state) mips_memcpy(state, &g_wireless, sizeof(*state));
                    return rc;
                }
                if (!has_bond && !(g_wireless.flags & TITAN_PADEMU_WIRELESS_SSP_READY)) {
                    g_wireless.last_error = -710;
                    g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
                    rc = 0;
                    wireless_state_changed();
                    if (state) mips_memcpy(state, &g_wireless, sizeof(*state));
                    return rc;
                }
                pad = wireless_find_pad(a->address);
                if (pad >= 0 &&
                    (g_wireless_direct_pad == pad ||
                     pad_status_check(DS34BT_STATE_CONNECTED, pad) ||
                     pad_status_check(DS34BT_STATE_RUNNING, pad))) {
                    /* CONNECT is idempotent.  A second UI click must never issue
                     * HCI Create Connection against an ACL that already exists
                     * (controller status 0x0B -> historical Titan -1627). */
                    g_wireless.selected = index;
                    g_wireless.last_error = 0;
                    g_wireless.flags &= ~TITAN_PADEMU_WIRELESS_HAS_ERROR;
                    a->flags |= TITAN_PADEMU_WIRELESS_CONTROLLER;
                    if (pad_status_check(DS34BT_STATE_RUNNING, pad)) {
                        a->flags |= TITAN_PADEMU_WIRELESS_CONNECTED;
                        a->flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTING;
                        if (g_wireless_direct_pad == pad) g_wireless_direct_pad = -1;
                    } else {
                        g_wireless_direct_pad = pad;
                        a->flags |= TITAN_PADEMU_WIRELESS_CONNECTING;
                        a->flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTED;
                    }
                    pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_UI, 0,
                                        TITAN_WIRELESS_BA(18), index, (u32)pad);
                    rc = 1;
                } else {
                    pad = wireless_alloc_pad(a->address);
                    if (pad < 0) { g_wireless.last_error = -706; rc = 0; }
                    else {
                        g_wireless_direct_pad = pad;
                        g_wireless.selected = index;
                        a->flags |= TITAN_PADEMU_WIRELESS_CONNECTING | TITAN_PADEMU_WIRELESS_CONTROLLER;
                        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_UI, 0, TITAN_WIRELESS_BA(6), index, (u32)pad);
                        if (xbox_ble.state == TITAN_BLE_SCANNING) {
                            rc = TitanBleHogp_StopScan(&xbox_ble);
                            if (rc) {
                                g_wireless_wait_ble_stop = 2;
                                pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0, TITAN_WIRELESS_BA(6), index, 2u);
                            } else {
                                g_wireless_direct_pad = -1;
                                a->flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTING;
                                g_wireless.last_error = -712;
                            }
                        } else {
                            rc = hci_create_accessory_connection(a) >= 0;
                            if (!rc) { g_wireless_direct_pad = -1; a->flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTING; g_wireless.last_error = -707; }
                        }
                    }
                }
            }
            if (!rc) g_wireless.flags |= TITAN_PADEMU_WIRELESS_HAS_ERROR;
            wireless_state_changed();
        }
    } else if (command == TITAN_PADEMU_WIRELESS_DISCONNECT) {
        int found = 0;
        int pad;
        if (index >= g_wireless.count) { g_wireless.last_error = -708; rc = 0; }
        else {
            for (pad = 0; pad < MAX_PADS; pad++) {
                if (wireless_addr_equal(ds34pad[pad].bdaddr, g_wireless.accessories[index].address) &&
                    (pad_status_check(DS34BT_STATE_CONNECTED, pad) || pad_status_check(DS34BT_STATE_RUNNING, pad))) {
                    found = 1;
                    (void)hci_disconnect(ds34pad[pad].hci_handle);
                    break;
                }
            }
            if (!found && (g_wireless.accessories[index].flags & TITAN_PADEMU_WIRELESS_BLE)) {
                /* BLE backend currently reconnects itself; forgetting is the
                 * authoritative operation. Disconnect remains harmless. */
                found = 1;
            }
            g_wireless.accessories[index].flags &= (u8)~TITAN_PADEMU_WIRELESS_CONNECTED;
            wireless_state_changed();
        }
    } else if (command == TITAN_PADEMU_WIRELESS_FORGET) {
        if (index >= g_wireless.count) { g_wireless.last_error = -709; rc = 0; }
        else {
            TitanPademuWirelessAccessory *a = &g_wireless.accessories[index];
            int pad;
            for (pad = 0; pad < MAX_PADS; pad++)
                if (wireless_addr_equal(ds34pad[pad].bdaddr, a->address) &&
                    (pad_status_check(DS34BT_STATE_CONNECTED, pad) || pad_status_check(DS34BT_STATE_RUNNING, pad)))
                    (void)hci_disconnect(ds34pad[pad].hci_handle);
            if (a->flags & TITAN_PADEMU_WIRELESS_BLE) {
                mips_memset(&xbox_ble_bond, 0, sizeof(xbox_ble_bond));
                mips_memset(&xbox_ble.bond, 0, sizeof(xbox_ble.bond));
                g_xbox_bond_dirty = 1;
                g_bond_generation++; if (!g_bond_generation) g_bond_generation = 1u;
            } else wireless_remove_bond(a->address);
            a->flags &= (u8)~(TITAN_PADEMU_WIRELESS_PAIRED | TITAN_PADEMU_WIRELESS_CONNECTED | TITAN_PADEMU_WIRELESS_CONNECTING);
            wireless_flush_bonds();
            wireless_state_changed();
        }
    }

    wireless_refresh_live_state();
    if (state) mips_memcpy(state, &g_wireless, sizeof(*state));
    return rc;
}

void ds34bt_reset()
{
    xbox_ble_init_transport();
    g_wireless_scan_pending = 0;
    g_wireless_scan_requested = 0;
    g_wireless_bootstrap_scan_pending = 0;
    g_wireless_hci_startup_ready = 0;
    g_wireless_ssp_event_mask = 0;
    g_wireless_ssp_mode = 0;
    g_wireless_wait_ble_stop = 0;

    int pad;

    if (bt_dev.status & DS34BT_STATE_USB_AUTHORIZED) {
        for (pad = 0; pad < MAX_PADS; pad++) {
            WaitSema(bt_dev.hid_sema);
            pad_status_set(DS34BT_STATE_DISCONNECT_REQUEST, pad);
            SignalSema(bt_dev.hid_sema);
            while (1) {
                DelayThread(500);
                WaitSema(bt_dev.hid_sema);
                if (!pad_status_check(DS34BT_STATE_RUNNING, pad)) {
                    SignalSema(bt_dev.hid_sema);
                    break;
                }
                SignalSema(bt_dev.hid_sema);
            }
        }
        DelayThread(1000000);
        bt_disconnect(bt_dev.devId);
    }
}

int ds34bt_bond_sync(const TitanPademuBondSync *request, TitanPademuBondSync *state)
{
    u32 command = TITAN_PADEMU_BOND_SYNC_QUERY;
    u32 ack_generation = 0u;
    if (!state) return 0;
    if (request && request->version == TITAN_PADEMU_BOND_SYNC_VERSION) {
        command = request->command;
        ack_generation = request->generation;
    }

    if (command == TITAN_PADEMU_BOND_SYNC_IMPORT && request) {
        if ((request->flags & TITAN_PADEMU_BOND_SYNC_CLASSIC_VALID) &&
            request->classic.magic == TITAN_CLASSIC_BOND_MAGIC &&
            request->classic.version == TITAN_CLASSIC_BOND_VERSION &&
            request->classic.count <= TITAN_PADEMU_WIRELESS_MAX_ACCESSORIES) {
            mips_memcpy(&g_classic_bonds, &request->classic, sizeof(g_classic_bonds));
        } else {
            mips_memset(&g_classic_bonds, 0, sizeof(g_classic_bonds));
            g_classic_bonds.magic = TITAN_CLASSIC_BOND_MAGIC;
            g_classic_bonds.version = TITAN_CLASSIC_BOND_VERSION;
        }
        g_classic_bonds_loaded = 1;
        g_classic_bonds_dirty = 0;
        if ((request->flags & TITAN_PADEMU_BOND_SYNC_XBOX_VALID) &&
            request->xbox.magic == TITAN_PADEMU_XBOX_BOND_MAGIC)
            mips_memcpy(&xbox_ble_bond, &request->xbox, sizeof(xbox_ble_bond));
        else
            mips_memset(&xbox_ble_bond, 0, sizeof(xbox_ble_bond));
        g_xbox_bond_dirty = 0;
        wireless_publish_bonds();
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0, TITAN_WIRELESS_BA(15),
                            request->flags, request->classic.count);
    } else if (command == TITAN_PADEMU_BOND_SYNC_ACK) {
        if (ack_generation == g_bond_generation) {
            g_classic_bonds_dirty = 0;
            g_xbox_bond_dirty = 0;
        }
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING, 0, TITAN_WIRELESS_BA(16),
                            ack_generation, g_bond_generation);
    }

    mips_memset(state, 0, sizeof(*state));
    state->version = TITAN_PADEMU_BOND_SYNC_VERSION;
    state->command = command;
    state->generation = g_bond_generation;
    if (g_classic_bonds.magic == TITAN_CLASSIC_BOND_MAGIC &&
        g_classic_bonds.version == TITAN_CLASSIC_BOND_VERSION) {
        state->flags |= TITAN_PADEMU_BOND_SYNC_CLASSIC_VALID;
        mips_memcpy(&state->classic, &g_classic_bonds, sizeof(state->classic));
    }
    if (xbox_ble_bond.magic == TITAN_BLE_BOND_VALID) {
        state->flags |= TITAN_PADEMU_BOND_SYNC_XBOX_VALID;
        mips_memcpy(&state->xbox, &xbox_ble_bond, sizeof(state->xbox));
    }
    if (g_classic_bonds_dirty) state->flags |= TITAN_PADEMU_BOND_SYNC_CLASSIC_DIRTY;
    if (g_xbox_bond_dirty) state->flags |= TITAN_PADEMU_BOND_SYNC_XBOX_DIRTY;
    return 1;
}

int ds34bt_get_telemetry(int port, void *out)
{
    TitanPademuPadTelemetry *t = (TitanPademuPadTelemetry *)out;
    ds34bt_pad_t *p;
    int i;

    if (!t || port < 0 || port >= MAX_PADS)
        return 0;

    p1064_l2cap_rx_recover_if_needed(4u);

    p = &ds34pad[port];
    mips_memset(t, 0, sizeof(*t));
    t->port=(u8)port;t->transport=TITAN_PADEMU_TRANSPORT_BT;t->connected=pad_status_check(DS34BT_STATE_RUNNING,port)?1:0;
    t->system_button=p->system_button;t->battery_state=p->battery_state;t->battery_percent=p->battery_percent;t->battery_valid=p->battery_valid;t->battery_raw=p->battery_raw;
    t->generation=p->telemetry_generation;t->packets=p->telemetry_packets;t->identity_quality=2;for(i=0;i<6;i++)t->address[i]=p->bdaddr[i];
    t->stable_key=2166136261u;for(i=0;i<6;i++){t->stable_key^=p->bdaddr[i];t->stable_key*=16777619u;}
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

int ds34bt_set_xbox_ble_bond(const TitanBleBond *bond) { if(!bond||bond->magic!=TITAN_BLE_BOND_VALID)return 0; xbox_ble_bond=*bond; return 1; }
int ds34bt_get_xbox_ble_bond(TitanBleBond *bond) { if(!bond||xbox_ble_bond.magic!=TITAN_BLE_BOND_VALID)return 0; *bond=xbox_ble_bond; return 1; }

int ds34bt_get_transport_status(TitanPademuBluetoothTransportStatus *out)
{
    if (!out) return 0;
    g_bt_transport.usb_dev_id = bt_dev.devId;
    g_bt_transport.usb_status = bt_dev.status;
    bt_diag_endpoints();
    *out = g_bt_transport;
    return 1;
}

int ds34bt_pairing_available(void)
{
    return (bt_dev.status & DS34BT_STATE_USB_CONFIGURED) != 0;
}

int ds34bt_begin_pairing(void)
{
    TitanBleState state;
    
    /* P9.2: arm Sony USB provisioning on every explicit Pair request. */
    sony_pair_requested = 1;
    pademu_trace_record(TITAN_PADEMU_TRACE_BT_PAIR_REQUEST, 0,
                        0x53500000u, (u32)sony_host_bdaddr_valid,
                        (u32)chrg_pid);
    if (sony_host_bdaddr_valid && chrg_end >= 0)
        (void)sony_pair_submit();
pademu_trace_record(TITAN_PADEMU_TRACE_BT_PAIR_REQUEST, 0, (u32)bt_dev.status, g_bt_transport.stage, g_bt_transport.endpoints_mask);
    if (!ds34bt_pairing_available()) {
        pademu_trace_record(TITAN_PADEMU_TRACE_BT_PAIR_REQUEST, -1, (u32)bt_dev.status, g_bt_transport.stage, g_bt_transport.endpoints_mask);
        return 0;
    }

    /* DS34BT_STATE_USB_CONFIGURED is asserted only after the controller has
       completed HCI WRITE_SCAN_ENABLE successfully.  Therefore the classic
       Sony path (DS3/DS4/DualSense) is already page-scannable here.

       Do NOT submit WRITE_SCAN_ENABLE again from the pairing RPC: the HCI
       control endpoint may legitimately have a LE command in flight, turning
       an already-listening Sony transport into a false "pairing failed". */

    /* Xbox Series BLE discovery is independent from Sony Classic BT.  Keep it
       best-effort: failure to restart BLE scanning must never invalidate a
       ready/page-scannable DualSense/DS4 transport.  In normal boot the BLE
       scanner is already started when HCI becomes READY. */
    state = xbox_ble.state;
    if (state != TITAN_BLE_SCANNING && state != TITAN_BLE_CONNECTED &&
        state != TITAN_BLE_PAIRING && state != TITAN_BLE_ENCRYPTING &&
        state != TITAN_BLE_GATT_SERVICE && state != TITAN_BLE_GATT_CHARS &&
        state != TITAN_BLE_GATT_DESCS && state != TITAN_BLE_READY) {
        (void)TitanBleHogp_StartScan(&xbox_ble);
    }

    return 1;
}
