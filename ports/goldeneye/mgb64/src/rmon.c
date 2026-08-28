#ifdef NATIVE_PORT
#include <stdarg.h>
#include <stdio.h>
#include <ultra64.h>
#include "crash.h"
#include "rmon.h"

/*
 * Native remote-monitor compatibility shim.
 *
 * The original game has host-debug entry points for token checks, host I/O, and
 * formatted logging. The native port does not talk to an N64 remote monitor, so
 * keep the ABI surface small and project-owned here.
 */

void rmonMain(void)
{
}

s32 rmonGetToken(void)
{
#if defined(DEBUG)
    return FALSE;
#else
    return TRUE;
#endif
}

s32 rmonStatus(void)
{
    return -1;
}

void osWriteHost(void *buffer, u32 size)
{
    (void)buffer;
    (void)size;
}

void osReadHost(void *buffer, u32 size)
{
    (void)buffer;
    (void)size;
}

void rmon7000CEC8(void)
{
}

void rmon7000CED0(void)
{
}

void rmon7000CED8(void)
{
}

void rmonPrintf(void)
{
}

void osSyncPrintf(const char *fmt, ...)
{
#ifndef _FINALROM
    char buffer[1024];
    int written;
    int i;
    va_list args;

    va_start(args, fmt);
    written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (written < 0) {
        return;
    }

    if (written >= (int)sizeof(buffer)) {
        written = (int)sizeof(buffer) - 1;
    }

    for (i = 0; i < written; i++) {
        crashAppendChar((u8)buffer[i]);
    }
#else
    (void)fmt;
#endif
}

#else
#include <stdarg.h>
#include <ultra64.h>
#include <PR/os.h>
#include "libultra/libc/xstdio.h"
#include "crash.h"
#include "rmon.h" /*<PR/rmon.h>*/

#ifdef ENABLE_USB

#include <os_internal.h>
#include "libultra/os/osint.h"
#include "usb.h"

#define RMON_MESSAGE_QUEUE_SIZE 10
#define RMON_MESSAGE_PRINTF_BUFFER_LENGTH 56

enum RMON_MESG_TYPE {
    RMON_MESG_TEST = 1,
    RMON_MESG_PRINTF
};

struct RmonMesgBase {
    int type;
};

struct RmonMesgPrintf {
    int type;
    int length;
    char buffer[RMON_MESSAGE_PRINTF_BUFFER_LENGTH];
};

int rmonCurrentMesgStack;
struct RmonMesgBase rmonMesgStack[2];
int rmonCurrentMesgPrintfStack;
struct RmonMesgPrintf rmonMesgPrintfStack[2];
OSMesg rmonMesg[RMON_MESSAGE_QUEUE_SIZE];
OSMesgQueue rmonMessageQueue;

OSTimer rmonSleeptimer;
OSMesgQueue rmonSleepMq;
OSMesg rmonSleepMesg;

void rmonHandleTestMsg(struct RmonMesgBase *msg);
void rmonHandlePrintfMsg(struct RmonMesgPrintf *msg);

// sprintf internal function to unwrap paramaters
extern char *proutSprintf(char *dst, const char *src, size_t count);

static u8 *proutSprintfAdapter(u8 *dst, const u8 *src, size_t count)
{
    return (u8 *)proutSprintf((char *)dst, (const char *)src, count);
}

void rmonInit(void) {
    rmonCurrentMesgStack = 0;
    rmonCurrentMesgPrintfStack = 0;

    osCreateMesgQueue(&rmonMessageQueue, &rmonMesg, RMON_MESSAGE_QUEUE_SIZE);
    osCreateMesgQueue(&rmonSleepMq, &rmonSleepMesg, 1);
}
#endif
/************************************************************************
Function: rmonMain
Args: none
Type: void
Purpose: This is the main loop of the rmon debugger. It is mostly a protocol
	parser/dispatcher.
************************************************************************/
void rmonMain(void) {
#ifdef ENABLE_USB
    rmonInit();

    while (1)
    {
        OSMesg work;
        struct RmonMesgBase *msg;

        while (0 == osRecvMesg(&rmonMessageQueue, &work, OS_MESG_NOBLOCK))
        {
            msg = work;

            switch (msg->type)
        {
                case RMON_MESG_TEST:
                    rmonHandleTestMsg((struct RmonMesgBase *)msg);
                break;

                case RMON_MESG_PRINTF:
                    rmonHandlePrintfMsg((struct RmonMesgPrintf *)msg);
                break;

                default:
                break;
        }
        }

        osSetTimer(&rmonSleeptimer, OS_USEC_TO_CYCLES(1000000), 0, &rmonSleepMq, rmonSleepMesg);
        osRecvMesg(&rmonSleepMq, &rmonSleepMesg, OS_MESG_BLOCK);
    }
#    endif
}

/**
 * rmonGetToken
 * returns true if this if rmon is disabled
 */
s32 rmonGetToken(void) {
    #if defined(DEBUG)
        //flesh out sending a token from pc app
        return FALSE;
    #else
        return TRUE;
    #endif
}

/**
 * REMOVED
 * rmonStatus
 * returns the status of rmon
 */
s32 rmonStatus(void) {
    #ifdef DEBUG
        //removed
    #else
        return -1;
    #endif

}



/**
 * Removed
 * reimpliment osWriteHost
 * target rmon and usb
*/
void osWriteHost(void * buffer, u32 size)
{
    if (buffer);
	if (size);
    #ifdef ENABLE_USB
    //flesh out a proper ge parser on pc app
    usb_write(DATATYPE_RMONBINARY  , buffer, size);
    #endif
}


/**
 * Removed
 * reimpliment osReadHost
 * target rmon and usb
*/
void osReadHost(void * buffer, u32 size)
{
#ifdef ENABLE_USB
    char incoming_type = 0;
    int incoming_size = 0;
#endif
    if (buffer);
	if (size);
#ifdef ENABLE_USB
    //flesh out a proper pc side tool still
    osSyncPrintf("USB: Waiting for data\n");
    while(1)
    {
        // Check if there's data in USB
        // Needs to be a while loop because we can't write to USB if there's data that needs to be read first
        while (usb_poll() != 0)
        {
            int header = usb_poll();
            // Store the size and type from the header
            incoming_type = USBHEADER_GETTYPE(header);
            incoming_size = USBHEADER_GETSIZE(header);
            osSyncPrintf("USB: Received header %d\n", incoming_type);
            osSyncPrintf("USB: Received size %d\n", incoming_size);
            // Ensure we're receiving a text command
            if (incoming_type != DATATYPE_RMONBINARY)
            {
                //errortype = 1;
                usb_purge();
                osSyncPrintf("USB: Received invalid type %d\n", incoming_type);
                return;
            }
            osSyncPrintf("USB: Read data\n");
            usb_read(buffer, size);
            return;
        }
    }
#endif
}


/**
 * Removed
 * rmon7000CEC8
 * unknown function
 */
void rmon7000CEC8(void) {
    #ifdef DEBUG
        //removed
    #endif
}

/**
 * Removed
 * rmon7000CED0
 * unknown function
 */
void rmon7000CED0(void) {
    #ifdef DEBUG
        //removed
    #endif
}

/**
 * Removed
 * rmon7000CED8
 * unknown function
 */
void rmon7000CED8(void) {
    #ifdef DEBUG
        //removed
    #endif
}



/**
 * Removed
 * rmonPrintf
 */
void rmonPrintf(void)
{
}

/**
 * Send text through the game's debug-print path.
 */
#ifndef ENABLE_USB
static u8 *proutSyncPrintf(u8 *str, const u8 *buf, size_t n)
{
    u32 sent = 0;

    (void)str;

#ifndef _FINALROM
    while (sent < n)
    {
#    ifdef USERDB
        sent += __osRdbSend((u8 *)&buf[sent], n - sent, RDB_TYPE_GtoH_PRINT);
#    endif
        crashAppendChar(buf[sent++]);
    }
#endif
    return (u8 *)1; /* return a fake pointer so that it's not NULL */
}
#endif


/**
 * Print formatted string to Debugger
 */
void osSyncPrintf(const char *fmt, ...)
{

#ifdef ENABLE_USB
    u8 buffer[100];
#endif
    int     ans;
    va_list args;
    va_start(args, fmt);
#ifdef ENABLE_USB
    ans = _Printf(proutSprintfAdapter, buffer, (const u8 *)fmt, args);
#else
    ans = _Printf(proutSyncPrintf, NULL, (const u8 *)fmt, args);
#endif
    va_end(args);
#ifdef ENABLE_USB
    if (ans >= 0) {
        if (ans > 99)
        {
            ans = 99;
        }
        buffer[ans] = 0;
    }
    usb_write_text(buffer, ans);
#endif

}

#ifdef ENABLE_USB
/**
 * Print formatted string to Debugger.
 * This is a non blocking call. The printf message will be copied
 * to the rmon printf queue. It is possible for the queue to overflow (wrap around).
 * The printf message is copied to a local byte array, truncating the message if necessary.
 */
void osAsyncPrintf(const char *format, ...)
{
    s32 written;
    va_list args;

    struct RmonMesgPrintf *dest = &rmonMesgPrintfStack[rmonCurrentMesgPrintfStack];
    rmonCurrentMesgPrintfStack++;
    if (rmonCurrentMesgPrintfStack > 1) {
        rmonCurrentMesgPrintfStack = 0;
    }
    dest->type = RMON_MESG_PRINTF;

    va_start(args, format);
    written = _Printf(proutSprintf, dest->buffer, format, args);
    va_end(args);
    if (written >= 0) {
        if (written > RMON_MESSAGE_PRINTF_BUFFER_LENGTH - 1)
        {
            written = RMON_MESSAGE_PRINTF_BUFFER_LENGTH - 1;
        }
        dest->buffer[written] = 0;
    }

    dest->length = written;

    osSendMesg(&rmonMessageQueue, (OSMesg)dest, OS_MESG_NOBLOCK);
}

void osQueueTest(void)
{
    struct RmonMesgBase *dest = &rmonMesgStack[rmonCurrentMesgStack];
    rmonCurrentMesgStack++;
    if (rmonCurrentMesgStack > 1) {
        rmonCurrentMesgStack = 0;
    }
    dest->type = RMON_MESG_TEST;
    osSendMesg(&rmonMessageQueue, (OSMesg)dest, OS_MESG_NOBLOCK);
}

void rmonHandleTestMsg(struct RmonMesgBase *msg)
{
    usb_write_text("????\n", 6);
}

void rmonHandlePrintfMsg(struct RmonMesgPrintf *msg)
{
    // pending read data will abort write, so clobber any incoming data.
    //usb_purge();
    usb_write_text(msg->buffer, msg->length);
}
#endif
#endif /* !NATIVE_PORT */
