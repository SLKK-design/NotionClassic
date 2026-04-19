/*
 * net_sockets_mac.c - MacTCP backend for mbedTLS net_sockets API
 *
 * Implements mbedtls_net_* using MacTCPHelper (TCPHi / CvtAddr).
 * The mbedtls_net_context::fd field stores the MacTCP stream identifier.
 * The mbedtls_net_context::give_time_ptr field stores the GiveTimePtr callback.
 *
 * Receive strategy (OS 7 vs OS 8+ split):
 *   OS 7 / MacTCP:  TCPRcv (RecvData) in CLOSE_WAIT consumes data from
 *     MacTCP's internal buffer without copying it into rcvBuff.  We bypass
 *     TCPRcv entirely and use LowTCPNoCopyRcv, which returns RDS pointers
 *     directly into MacTCP's internal buffer.
 *   OS 8+ / Open Transport:  RecvData works correctly.  LowTCPNoCopyRcv
 *     is not reliably supported via the OT MacTCP compatibility shim, so
 *     we keep the original RecvData path here.
 */

#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>
#include <string.h>
#include <stdlib.h>

#include <MacTCP.h>
/* Include <Devices.h> for PBControl (used by tcp_connection_state /
 * tcp_open_async).  Guard CFM's CloseConnection first so it doesn't
 * conflict with TCPHi.h's CloseConnection(unsigned long, ...) below. */
#define CloseConnection __cfm_CloseConnection
#include <Devices.h>
#undef CloseConnection
#include <mactcp/TCPHi.h>
#include <mactcp/CvtAddr.h>
/* TCPRoutines.h declares LowTCPNoCopyRcv / LowTCPBfrReturn. */
#include <mactcp/TCPRoutines.h>

#define NET_BUF_SIZE   8192
#define NOCOPY_MAX_RDS 8

/* Low-memory global: system version as packed BCD, e.g. 0x0750 = System 7.5 */
#define MAC_SYS_VERSION  (*(short *)0x15A)

static int _cancel = 0;

/* Last raw MacTCP error — readable from C++ for diagnostics */
OSErr gLastMacTCPErr = 0;

/* Last OSErr seen in mbedtls_net_recv */
OSErr gLastRecvErr = 0;

/* Bytes delivered on most recent mbedtls_net_recv call */
unsigned short gLastAmtUnread = 0;

/* ---------------------------------------------------------------------------
 * recv_nocopy — OS 7 / MacTCP receive path.
 *
 * Uses LowTCPNoCopyRcv to read directly from MacTCP's internal RDS buffer,
 * bypassing the TCPRcv copy step that is broken on OS 7 in CLOSE_WAIT.
 * --------------------------------------------------------------------------*/
static int recv_nocopy(unsigned long stream, unsigned char *buf, size_t len,
                       GiveTimePtr giveTime)
{
    rdsEntry rds[NOCOPY_MAX_RDS + 1];
    Boolean urgent = false, mark = false;
    TCPiopb *returnBlock = NULL;
    memset(rds, 0, sizeof(rds));

    OSErr err = LowTCPNoCopyRcv((StreamPtr)stream,
                                  30,            /* 30-second timeout */
                                  &urgent, &mark,
                                  (Ptr)rds, NOCOPY_MAX_RDS,
                                  false,         /* synchronous */
                                  &returnBlock,
                                  giveTime, &_cancel);
    gLastRecvErr = err;

    if (err == connectionTerminated)
        return -1;  /* signal caller to set ctx->terminated */

    if (err != noErr && err != connectionClosing)
        return MBEDTLS_ERR_NET_RECV_FAILED;

    unsigned short copied = 0;
    int i;
    for (i = 0; i < NOCOPY_MAX_RDS && rds[i].length > 0; i++) {
        unsigned short n = rds[i].length;
        if (n > (unsigned short)(len - copied))
            n = (unsigned short)(len - copied);
        memcpy(buf + copied, (void *)rds[i].ptr, n);
        copied += n;
        if (copied >= (unsigned short)len)
            break;
    }

    LowTCPBfrReturn((StreamPtr)stream, (Ptr)rds, giveTime, &_cancel);

    gLastAmtUnread = copied;
    return (int)copied;   /* 0 = EOF (connectionClosing with no data) */
}

/* ---------------------------------------------------------------------------
 * recv_standard — OS 8+ / Open Transport receive path.
 *
 * Uses RecvData (TCPRcv) which works correctly on Open Transport.
 * --------------------------------------------------------------------------*/
static int recv_standard(unsigned long stream, unsigned char *buf, size_t len,
                         GiveTimePtr giveTime)
{
    unsigned short dataLen = (unsigned short)(len > 65535 ? 65535 : len);

    OSErr err = RecvData(stream, (Ptr)buf, &dataLen, false, giveTime, &_cancel);
    gLastRecvErr = err;

    if (err == connectionTerminated)
        return -1;  /* signal caller to set ctx->terminated */

    if (err == connectionClosing) {
        gLastAmtUnread = dataLen;
        return (int)dataLen;   /* OT delivers data correctly on connectionClosing */
    }

    if (err != noErr)
        return MBEDTLS_ERR_NET_RECV_FAILED;

    gLastAmtUnread = dataLen;
    return (int)dataLen;
}

void mbedtls_net_init(mbedtls_net_context *ctx, void *give_time_ptr)
{
    ctx->fd = -1;
    ctx->give_time_ptr = give_time_ptr;
    ctx->terminated = 0;
}

int mbedtls_net_connect(mbedtls_net_context *ctx, const char *host,
                        const char *port, int proto)
{
    OSErr err;
    unsigned long ipAddress;
    unsigned long stream;
    short portNum = (short)atoi(port);
    GiveTimePtr giveTime = (GiveTimePtr)ctx->give_time_ptr;

    err = InitNetwork();
    if (err != noErr)
        return MBEDTLS_ERR_NET_SOCKET_FAILED;

    err = ConvertStringToAddr((char *)host, &ipAddress, giveTime);
    if (err != noErr) {
        gLastMacTCPErr = err;
        return MBEDTLS_ERR_NET_UNKNOWN_HOST;
    }

    err = CreateStream(&stream, NET_BUF_SIZE, giveTime, &_cancel);
    if (err != noErr)
        return MBEDTLS_ERR_NET_SOCKET_FAILED;

    err = OpenConnection(stream, (long)ipAddress, portNum, 0, giveTime, &_cancel);
    if (err != noErr) {
        ReleaseStream(stream, giveTime, &_cancel);
        return MBEDTLS_ERR_NET_CONNECT_FAILED;
    }

    ctx->fd = (int)stream;
    return 0;
}

int mbedtls_net_send(void *ctx_v, const unsigned char *buf, size_t len)
{
    mbedtls_net_context *ctx = (mbedtls_net_context *)ctx_v;
    GiveTimePtr giveTime = (GiveTimePtr)ctx->give_time_ptr;
    unsigned long stream = (unsigned long)ctx->fd;
    OSErr err;

    if (ctx->fd < 0)
        return MBEDTLS_ERR_NET_INVALID_CONTEXT;

    err = SendData(stream, (Ptr)buf, (unsigned short)len, 0, giveTime, &_cancel);
    if (err != noErr)
        return MBEDTLS_ERR_NET_SEND_FAILED;

    return (int)len;
}

int mbedtls_net_recv(void *ctx_v, unsigned char *buf, size_t len)
{
    mbedtls_net_context *ctx = (mbedtls_net_context *)ctx_v;
    GiveTimePtr giveTime = (GiveTimePtr)ctx->give_time_ptr;
    unsigned long stream = (unsigned long)ctx->fd;

    if (ctx->fd < 0)
        return MBEDTLS_ERR_NET_INVALID_CONTEXT;

    int n = recv_standard(stream, buf, len, giveTime);

    /* -1 is the internal signal for connectionTerminated (RST from peer) */
    if (n == -1) {
        ctx->terminated = 1;
        return 0;
    }
    return n;
}

void mbedtls_net_close(mbedtls_net_context *ctx)
{
    if (ctx->fd < 0)
        return;
    GiveTimePtr giveTime = (GiveTimePtr)ctx->give_time_ptr;
    unsigned long stream = (unsigned long)ctx->fd;
    CloseConnection(stream, giveTime, &_cancel);
    ctx->fd = -1;
}

void mbedtls_net_free(mbedtls_net_context *ctx)
{
    if (ctx->fd < 0)
        return;
    GiveTimePtr giveTime = (GiveTimePtr)ctx->give_time_ptr;
    unsigned long stream = (unsigned long)ctx->fd;
    /* Skip graceful close when peer already sent RST — CloseConnection()
     * can block for MacTCP's full close-wait timeout on a dead stream. */
    if (!ctx->terminated)
        CloseConnection(stream, giveTime, &_cancel);
    ReleaseStream(stream, giveTime, &_cancel);
    ctx->fd = -1;
    ctx->terminated = 0;
}

/* ------------------------------------------------------------------ */
/* OPT-1: TCP connection health check                                 */
/* Returns the connectionState byte from TCPStatus:                   */
/*   0 = CLOSED, 6 = SYN_SENT, 8 = ESTABLISHED, 14 = CLOSE_WAIT     */
/* ------------------------------------------------------------------ */
/* refNum is a non-static global in TCPRoutines.c, initialised by
 * OpenTCPDriver() (called via InitNetwork).  It must remain non-static
 * there; changing it to static would silently break this linkage.     */
extern short refNum;

short tcp_connection_state(unsigned long stream)
{
    TCPiopb pb;
    memset(&pb, 0, sizeof(pb));
    pb.ioCRefNum  = refNum;
    pb.ioResult   = 1;
    pb.tcpStream  = (StreamPtr)stream;
    pb.csCode     = TCPStatus;
    PBControl((ParmBlkPtr)&pb, false); /* synchronous: fast status query */
    if (pb.ioResult != noErr)
        return 0;
    return (short)pb.csParam.status.connectionState;
}

/* ------------------------------------------------------------------ */
/* OPT-6: Async TCPActiveOpen — fires the SYN without blocking.       */
/* The static pBlock is valid until tcp_wait_connected() consumes it. */
/* ------------------------------------------------------------------ */
static TCPiopb g_async_open_pb;
static int     g_async_open_pending = 0;

int tcp_open_async(unsigned long stream, unsigned long ipAddress, short port)
{
    memset(&g_async_open_pb, 0, sizeof(g_async_open_pb));
    g_async_open_pb.ioCRefNum  = refNum;
    g_async_open_pb.ioCompletion = nil;
    g_async_open_pb.ioResult   = 1;
    g_async_open_pb.tcpStream  = (StreamPtr)stream;
    g_async_open_pb.csCode     = TCPActiveOpen;
    g_async_open_pb.csParam.open.ulpTimeoutValue    = 30;
    g_async_open_pb.csParam.open.ulpTimeoutAction   = 1;
    g_async_open_pb.csParam.open.validityFlags      = 0xC0;
    g_async_open_pb.csParam.open.commandTimeoutValue = 30;
    g_async_open_pb.csParam.open.remoteHost = (ip_addr)ipAddress;
    g_async_open_pb.csParam.open.remotePort = (tcp_port)port;
    g_async_open_pb.csParam.open.localPort  = 0;

    PBControl((ParmBlkPtr)&g_async_open_pb, true); /* async: returns immediately */
    g_async_open_pending = 1;
    return 0;
}

/* OPT-6: Poll until TCP is ESTABLISHED (or error / cancel).          */
OSErr tcp_wait_connected(GiveTimePtr giveTime, bool *cancel)
{
    if (!g_async_open_pending)
        return -1;

    while (g_async_open_pb.ioResult > 0) {
        giveTime();
        if (*cancel) {
            g_async_open_pending = 0;
            gLastMacTCPErr = -1;
            return -1;
        }
    }

    g_async_open_pending = 0;
    OSErr err = g_async_open_pb.ioResult;
    gLastMacTCPErr = err;
    return err;
}
