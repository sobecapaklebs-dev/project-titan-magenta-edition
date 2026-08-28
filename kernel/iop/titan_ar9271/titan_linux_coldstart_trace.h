#ifndef TITAN_LINUX_COLDSTART_TRACE_H
#define TITAN_LINUX_COLDSTART_TRACE_H
#include <types.h>
#define TITAN_LINUX_TRACE_KIND_WMI 1u
#define TITAN_LINUX_TRACE_KIND_MGMT 2u
#define TITAN_LINUX_TRACE_EVENT_COUNT 3928u
#define TITAN_LINUX_TRACE_WMI_COUNT 3912u
#define TITAN_LINUX_TRACE_MGMT_COUNT 16u
#define TITAN_LINUX_TRACE_LAST_WMI_SEQ 3912u
#define TITAN_LINUX_TRACE_CAPTURE_DURATION_US 2212017u
#define TITAN_LINUX_TRACE_DELAY_BUDGET_US 1041716u
#define TITAN_LINUX_TRACE_RSP_LEN_SHA256 "6d54ba07f4b9fa61b2805382e991ff3bd30c7dc96ecc1ae3b4cafd9ea4562938"
#define TITAN_LINUX_TRACE_RSP_BLOB_SIZE 7372u
#define TITAN_LINUX_TRACE_RSP_BLOB_SHA256 "50a89728c2feb8f45db00cdc7e08ae96efd14729485d07c77958a0609d120a01"
#define TITAN_LINUX_TRACE_RSP_EXACT_SHA256 "b57cc16899d576ea3473b9bd8feaf2a06199d7f0e78866a009f4ce4ac54f5a66"
#define TITAN_LINUX_POSTASSOC_EVENT_COUNT 22u
#define TITAN_LINUX_POSTASSOC_LAST_WMI_SEQ 3934u
#define TITAN_LINUX_TRACE_MAX_WMI_WIRE 508u
#define TITAN_LINUX_TRACE_PCAP_SHA256 "059e458a49c357a5a0e89b5b522f6a965a41fc06fb3e08315d0dc1a3e8f9f1ee"
#define TITAN_LINUX_TRACE_CANON_SHA256 "1511665dfaa438d5cc1c05731e7c6f1a09760e2c0836846078829eb32a2fe84b"
#define TITAN_LINUX_POSTASSOC_CANON_SHA256 "b34ff05c62b626a9b6806aa7111656a343c3027beab2a95f68d30518f17d402b"
#define TITAN_LINUX_TRACE_BLOB_SHA256 "312c3e633f45eb90b2f40c0e051870abd6559dfa73f69becf51528c91cd61cdd"
#define TITAN_LINUX_TRACE_AUTH_SHA256 "f49a7d55a060358905af439bbfc2b11d98f650d8ef7474acab700b8d838a9ac7"
#define TITAN_LINUX_TRACE_ASSOC_SHA256 "6ee698c1a3bea589d348e84f092c41dae6e43743a9b455a198aa14a8f84337a2"
#define TITAN_LINUX_TRACE_AUTH_FNV1A32 0xe5ed47f2u
#define TITAN_LINUX_TRACE_ASSOC_FNV1A32 0x103f1d6bu
typedef struct TitanLinuxTraceEvent {
    u32 offset;
    u32 delta_us;
    u16 length;
    u16 id;
    u16 aux;
    u8 kind;
    u8 reserved;
} TitanLinuxTraceEvent;
extern const u8 titan_linux_trace_blob[];
extern const u32 titan_linux_trace_blob_size;
extern const TitanLinuxTraceEvent titan_linux_trace_events[TITAN_LINUX_TRACE_EVENT_COUNT];
extern const TitanLinuxTraceEvent titan_linux_postassoc_events[TITAN_LINUX_POSTASSOC_EVENT_COUNT];
/* Indexed by pre-association WMI sequence number (1..3912). Entry 0 is 0.
 * Values are response payload lengths after the four-byte WMI command header. */
extern const u8 titan_linux_trace_rsp_len[TITAN_LINUX_TRACE_WMI_COUNT + 1u];
/* Exact successful Linux response payloads, indexed by WMI sequence.
 * Offset table entry 0 is unused. For seq N, compare rsp_len[N] bytes
 * starting at rsp_blob + rsp_off[N]. */
extern const u16 titan_linux_trace_rsp_off[TITAN_LINUX_TRACE_WMI_COUNT + 1u];
extern const u8 titan_linux_trace_rsp_blob[TITAN_LINUX_TRACE_RSP_BLOB_SIZE];
#endif
