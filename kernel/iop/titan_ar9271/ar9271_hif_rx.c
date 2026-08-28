/*
 * AR9271 HIF USB RX stream parser for the Project Titan standalone tester.
 *
 * Derived from Linux ath9k_htc hif_usb.c RX stream framing.  Linux uses
 * ATH_USB_RX_STREAM_MODE_TAG (0x4e00), a little-endian length/tag header,
 * 32-bit padding, multiple packets per transfer and state for a packet that
 * spans transfers.  This PS2 port keeps those wire semantics while replacing
 * skb/URB storage with a fixed IOP buffer.
 *
 * SPDX-License-Identifier: ISC
 */
#include <types.h>
#include <sysclib.h>
#include "ar9271_hif_rx.h"

static u16 rd_le16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

void titan_hif_rx_init(TitanHifRx *hif, TitanHifPacketFn fn, void *ctx)
{
    memset(hif, 0, sizeof(*hif));
    hif->packet_fn = fn;
    hif->packet_ctx = ctx;
}

void titan_hif_rx_reset(TitanHifRx *hif)
{
    TitanHifPacketFn fn = hif->packet_fn;
    void *ctx = hif->packet_ctx;
    memset(hif, 0, sizeof(*hif));
    hif->packet_fn = fn;
    hif->packet_ctx = ctx;
}

void titan_hif_rx_feed(TitanHifRx *hif, const u8 *data, int len)
{
    int pos = 0;
    if (!hif || !data || len <= 0) return;

    if (len > TITAN_HIF_RX_STASH_SIZE - hif->stash_len) {
        /* Linux drops an invalid input skb instead of trying to salvage an
         * unbounded stream. Keep the same fail-closed behaviour here. */
        hif->invalid_packets++;
        hif->stash_len = 0;
        if (len > TITAN_HIF_RX_STASH_SIZE) return;
    }

    memcpy(hif->stash + hif->stash_len, data, len);
    hif->stash_len += len;

    while (hif->stash_len - pos >= 4) {
        const u8 *hdr = hif->stash + pos;
        int pkt_len = (int)rd_le16(hdr);
        u16 tag = rd_le16(hdr + 2);
        int pad_len;
        int total_len;

        if (tag != TITAN_HIF_RX_STREAM_TAG || pkt_len <= 0 ||
            pkt_len > TITAN_HIF_RX_MAX_PACKET) {
            /* Match ath9k_hif_usb_rx_stream: an invalid tag/length poisons the
             * current input stream instead of byte-scanning for a new tag. */
            hif->invalid_packets++;
            hif->stash_len = 0;
            return;
        }

        pad_len = 4 - (pkt_len & 3);
        if (pad_len == 4) pad_len = 0;
        total_len = 4 + pkt_len + pad_len;

        if (hif->stash_len - pos < total_len) {
            hif->partial_packets++;
            break;
        }

        if (hif->packet_fn)
            hif->packet_fn(hif->packet_ctx, hdr + 4, pkt_len);
        hif->packets++;
        pos += total_len;
    }

    if (pos > 0) {
        int remain = hif->stash_len - pos;
        if (remain > 0) memmove(hif->stash, hif->stash + pos, remain);
        hif->stash_len = remain;
    }
}
