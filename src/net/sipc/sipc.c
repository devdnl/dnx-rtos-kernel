/*==============================================================================
File     sipc.c

Author   Daniel Zorychta

Brief    SIPC Network management.

         Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

         This program is free software; you can redistribute it and/or modify
         it under the terms of the GNU General Public License as published by
         the Free Software Foundation and modified by the dnx RTOS exception.

         NOTE: The modification  to the GPL is  included to allow you to
               distribute a combined work that includes dnx RTOS without
               being obliged to provide the source  code for proprietary
               components outside of the dnx RTOS.

         The dnx RTOS  is  distributed  in the hope  that  it will be useful,
         but WITHOUT  ANY  WARRANTY;  without  even  the implied  warranty of
         MERCHANTABILITY  or  FITNESS  FOR  A  PARTICULAR  PURPOSE.  See  the
         GNU General Public License for more details.

         Full license text is available on the following file: doc/license.txt.


==============================================================================*/

/*
HOST                   CLIENT

Handshake communication:
HANDSHAKE -----------> (port opened)
(ok)      <----------- ACK

HANDSHAKE -----------> (port not opened)
(error)   <----------- NACK

BIND      -----------> (no action)

Data send (ACK):
DATA      -----------> buffer (empty) -> store
(ok)      <----------- ACK

Data send (NACK) - port not exists:
DATA      -----------> (port not opened)
(error)   <----------- NACK

Data send (BUSY) - buffer full:
DATA      -----------> (port opened) buffer (full capacity) -> store
(wait)    <----------- BUSY (buffer exceeds capacity)
(ok)      <----------- ACK  (buffer ready for data receive)

Data send (REPEAT) - checksum incorrect:
DATA      -----------> checksum incorrect
(ok)      <----------- REPEAT
DATA      -----------> store data
(ok)      <----------- ACK

*/

/*==============================================================================
  Include files
==============================================================================*/
#include <stdbool.h>
#include "net/sipc/sipc.h"
#include "sipcbuf.h"
#include "cpuctl.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define PACKET_TYPE_DATA          0
#define PACKET_TYPE_HANDSHAKE     1
#define PACKET_TYPE_HANDSHAKEACK  2
#define PACKET_TYPE_HANDSHAKENACK 3
#define PACKET_TYPE_DATABUSY      4
#define PACKET_TYPE_DATAREPEAT    5
#define PACKET_TYPE_BIND          6
#define PACKET_TYPE_DATAACK       7
#define PACKET_TYPE_DATANACK      8
#define PACKET_TYPE_REFUSE        9
#define _PACKET_TYPE_COUNT        10

/* maximum number of DATAREPEAT requests for single data chunk */
#define MAX_DATA_REPEAT 10

/* payload length limit used when interface was never configured (MTU = 0) */
#define DEFAULT_MAX_PAYLOAD 64

/* number of consecutive failed/empty reads after which payload drain stops */
#define DRAIN_MAX_FAILS 3

#define zalloc(_size, _pptr) _kzalloc(_MM_NET, _size, NULL, _MM_FLAG__DMA, _MM_FLAG__DMA, _pptr)
#define kalloc(_size, _pptr) _kmalloc(_MM_NET, _size, NULL, _MM_FLAG__DMA, _MM_FLAG__DMA, _pptr)
#define zfree(_pptr)         _kfree(_MM_NET, _pptr)

#if __NETWORK_SIPC_DEBUG_ON__ > 0
#define DEBUG(...) printk("SIPC: "__VA_ARGS__)
#else
#define DEBUG(...)
#endif

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        u8_t preamble[4];       /*!< Preamble */
        u16_t header_checksum;  /*!< Fletcher16 Checksum */
        u16_t plen;             /*!< Payload size */
        u16_t seq;              /*!< Sequence */
        u8_t port;              /*!< Port */
        u8_t type;              /*!< Packet type */
        u16_t payload_checksum; /*!< Payload checksum */
        u8_t payload[];         /*!< Payload */
} sipc_packet_t;

/* header is sent/received as raw memory - layout must not contain padding */
_Static_assert(sizeof(sipc_packet_t) == 14, "SIPC: unexpected packet header layout");

typedef struct {
        struct {
                u16_t MTU;
        } conf;

        struct {
                u64_t rx_packets;
                u64_t tx_packets;
                u64_t rx_bytes;
                u64_t tx_bytes;
        } stats;

        kfile_t *if_file;
        tid_t if_thread;
        volatile NETM_SIPC_state_t state;
        kmtx_t *packet_send_mtx;
        kmtx_t *socket_list_mtx;
        llist_t *socket_list;
        uint16_t seq_ctr;
        volatile bool run;
} sipc_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int input_thread(void *sem);
static void clear_transfer_counters(sipc_t *sipc);
static void unregister_socket(sipc_t *sipc, SIPC_socket_t *socket);
static int register_socket(sipc_t *sipc, SIPC_socket_t *socket);
static bool is_socket_registered(sipc_t *sipc, SIPC_socket_t *socket);
static int send_packet(sipc_t *sipc, u16_t seq, u8_t port, u8_t type, const u8_t *payload, u16_t plen);
static SIPC_socket_t *find_socket_by_port_locked(sipc_t *sipc, u8_t port);
static void handle_packet(sipc_t *sipc, SIPC_socket_t *socket, const sipc_packet_t *packet, u8_t *payload);

/*==============================================================================
  External function prototypes
==============================================================================*/

/*==============================================================================
  Local objects
==============================================================================*/
static const u8_t PACKET_PREABLE[4] = {0xAA, 0x55, 0xFF, 0x01};
static const u32_t CONNECTION_TIMEOUT = 4000;

#if __NETWORK_SIPC_DEBUG_ON__ > 0
static const char *TYPE_STR[] = {"DATA", "HSK", "HSKACK", "HSKNACK", "DATABUSY", "DATAREPT", "BIND", "DATAACK",
        "DATANACK", "REFUSE"};
#endif

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Clear received and transmitted byte counters
 */
//==============================================================================
static void clear_transfer_counters(sipc_t *sipc)
{
        sipc->stats.rx_bytes = 0;
        sipc->stats.tx_bytes = 0;
        sipc->stats.rx_packets = 0;
        sipc->stats.tx_packets = 0;
}

//==============================================================================
/**
 * @brief  Function compare sockets. Used in linked list.
 *
 * @param  a    socket a
 * @param  b    socket b
 *
 * @return 0 if equal, otherwise -1 or 1.
 */
//==============================================================================
static int list_cmp_functor(const void *a, const void *b)
{
        const SIPC_socket_t *socka = a;
        const SIPC_socket_t *sockb = b;

        if (socka->port == sockb->port) {
                return 0;

        } else if (socka->port > sockb->port) {
                return 1;

        } else {
                return -1;
        }
}

//==============================================================================
/**
 * @brief  Function calculate fletcher 16 checksum.
 *
 * @param  data         buffer
 * @param  bytes        buffer size
 *
 * @return Checksum.
 */
//==============================================================================
static uint16_t fletcher16(const void *buf, size_t bytes)
{
        const uint8_t *data = buf;
        uint16_t sum1 = 0xff, sum2 = 0xff;
        size_t tlen;

        while (bytes) {
                tlen = ((bytes >= 20) ? 20 : bytes);
                bytes -= tlen;
                do {
                        sum2 += sum1 += *data++;
                        tlen--;
                } while (tlen);

                sum1 = (sum1 & 0xff) + (sum1 >> 8);
                sum2 = (sum2 & 0xff) + (sum2 >> 8);
        }

        /* Second reduction step to reduce sums to 8 bits */
        sum1 = (sum1 & 0xff) + (sum1 >> 8);
        sum2 = (sum2 & 0xff) + (sum2 >> 8);

        return (sum2 << 8) | sum1;
}

//==============================================================================
/**
 * @brief  Function synchronize input stream with packet preamble.
 *
 * @param  sipc         stack context
 *
 * @return true when preamble was found, false when stack is stopping.
 */
//==============================================================================
static bool sync_preamble(sipc_t *sipc)
{
        size_t i = 0;

        while (sipc->run && (i < ARRAY_SIZE(PACKET_PREABLE))) {
                u8_t byte;
                size_t rdcnt = 0;

                if (sys_fread(&byte, 1, &rdcnt, sipc->if_file) != 0) {
                        i = 0;
                        sys_sleep_ms(10);
                        continue;
                }

                if (rdcnt != 1) {
                        i = 0;
                        continue;
                }

                if (byte == PACKET_PREABLE[i]) {
                        i++;
                } else {
                        // byte can be the beginning of a new preamble
                        i = (byte == PACKET_PREABLE[0]) ? 1 : 0;
                }
        }

        return sipc->run;
}

//==============================================================================
/**
 * @brief  Function discard selected number of bytes from input stream.
 *
 * Used when packet header is valid but payload cannot be received (too long
 * payload, no memory). Without draining, payload bytes would be interpreted
 * as a preamble of next packet.
 *
 * @param  sipc         stack context
 * @param  len          number of bytes to discard
 */
//==============================================================================
static void drain_payload(sipc_t *sipc, size_t len)
{
        u8_t tmp[32];
        int fails = 0;

        while (sipc->run && (len > 0)) {
                size_t chunk = min(len, sizeof(tmp));
                size_t rdcnt = 0;

                if ((sys_fread(tmp, chunk, &rdcnt, sipc->if_file) != 0) || (rdcnt == 0)) {
                        // stream broken or remote stopped sending - give up,
                        // next packet will be found by preamble synchronization
                        if (++fails > DRAIN_MAX_FAILS) {
                                DEBUG("payload drain aborted, %u bytes left", (unsigned)len);
                                break;
                        }
                        continue;
                }

                fails = 0;
                len -= min(rdcnt, len);
        }
}

//==============================================================================
/**
 * @brief  Function reject packet with valid header which payload cannot be
 *         received. Payload is discarded from the stream and, for DATA
 *         packets, sender is notified by DATANACK so it does not have to wait
 *         for the send timeout.
 *
 * @param  sipc         stack context
 * @param  phdr         received (valid) packet header
 */
//==============================================================================
static void reject_packet(sipc_t *sipc, const sipc_packet_t *phdr)
{
        drain_payload(sipc, phdr->plen);

        // in DOWN state incoming data is only consumed, nothing is answered
        if ((sipc->state != NETM_SIPC_STATE__DOWN) && (phdr->type == PACKET_TYPE_DATA)) {
                send_packet(sipc, phdr->seq, phdr->port, PACKET_TYPE_DATANACK, NULL, 0);
        }
}

//==============================================================================
/**
 * @brief  Function receive incoming packet.
 *
 * @param  sipc         stack context
 * @param  phdr         packet header container
 * @param  payload      allocated payload (NULL if packet without payload)
 *
 * @return true when valid packet header was received, false when stack is
 *         stopping (payload is NULL then).
 */
//==============================================================================
static bool receive_packet(sipc_t *sipc, sipc_packet_t *phdr, u8_t **payload)
{
        *payload = NULL;

        while (sipc->run) {

                if (!sync_preamble(sipc)) {
                        break;
                }

                // check sum + rest of header
                const size_t header_size = sizeof(sipc_packet_t);
                const size_t header_rest = header_size - sizeof(phdr->preamble);
                size_t rdcnt = 0;

                if ((sys_fread(&phdr->header_checksum, header_rest, &rdcnt, sipc->if_file) != 0) ||
                        (rdcnt != header_rest)) {
                        continue;
                }

                u16_t hdr_chsum = phdr->header_checksum;
                memcpy(phdr->preamble, PACKET_PREABLE, sizeof(phdr->preamble));
                phdr->header_checksum = 0;
                phdr->header_checksum = fletcher16(phdr, header_size);

                if (hdr_chsum != phdr->header_checksum) {
                        printk("SIPC: header checksum: %04Xh:%04Xh->INVALID", hdr_chsum, phdr->header_checksum);
                        continue; // resynchronize with next preamble
                }

                if (phdr->plen == 0) {
                        return true;
                }

                u16_t max_plen = sipc->conf.MTU ? sipc->conf.MTU : DEFAULT_MAX_PAYLOAD;
                if (phdr->plen > max_plen) {
                        printk("SIPC: payload too long (%u > %u bytes), packet dropped", (unsigned)phdr->plen,
                                (unsigned)max_plen);
                        reject_packet(sipc, phdr);
                        continue;
                }

                u8_t *buf = NULL;
                int err = zalloc(phdr->plen, (void *)&buf);
                if (!err) {
                        rdcnt = 0;
                        if ((sys_fread(buf, phdr->plen, &rdcnt, sipc->if_file) == 0) && (rdcnt == phdr->plen)) {

                                *payload = buf;
                                return true;
                        } else {
                                zfree((void *)&buf);
                        }
                } else {
                        printk("SIPC: no memory to receive packet payload (%u bytes)", (unsigned)phdr->plen);
                        reject_packet(sipc, phdr);
                }
        }

        return false;
}

//==============================================================================
/**
 * @brief  Function send packet.
 *
 * @param  sipc         stack context
 * @param  seq          sequence number
 * @param  port         packet port
 * @param  type         packet type
 * @param  payload      payload to send
 * @param  plen         payload length
 *
 * @return One of errno value.
 */
//==============================================================================
static int send_packet(sipc_t *sipc, u16_t seq, u8_t port, u8_t type, const u8_t *payload, u16_t plen)
{
        if (!sipc) {
                return ENONET;
        }

        DEBUG("sending packet : seq:%u, port:%d, type:%s, plen:%d", seq, port,
                type < _PACKET_TYPE_COUNT ? TYPE_STR[type] : "UNKNOWN", plen);

        if (sipc->state == NETM_SIPC_STATE__DOWN) {
                return ENONET;
        }

        if (!payload) {
                plen = 0;
        }

        sipc_packet_t packet;

        memcpy(packet.preamble, PACKET_PREABLE, sizeof(packet.preamble));
        packet.header_checksum = 0;
        packet.seq = seq;
        packet.port = port;
        packet.type = type;
        packet.plen = plen;
        packet.payload_checksum = plen ? fletcher16(payload, plen) : 0;
        packet.header_checksum = fletcher16(&packet, sizeof(packet));

        int err = sys_mutex_lock(sipc->packet_send_mtx, _WAIT_FOREVER_MS);
        if (!err) {

                size_t wrcnt = 0;
                err = sys_fwrite(&packet, sizeof(sipc_packet_t), &wrcnt, sipc->if_file);
                if (!err && (wrcnt != sizeof(sipc_packet_t))) {
                        err = EIO;
                }

                if (err) {
                        DEBUG("header send error: %d", err);

                } else if (plen) {
                        wrcnt = 0;
                        err = sys_fwrite(payload, plen, &wrcnt, sipc->if_file);
                        if (!err && (wrcnt != plen)) {
                                err = EIO;
                        }

                        if (err) {
                                DEBUG("payload send error: %d", err);
                        }
                }

                sipc->seq_ctr++;

                if (!err) {
                        sipc->stats.tx_packets++;
                        sipc->stats.tx_bytes += plen;
                }

                sys_mutex_unlock(sipc->packet_send_mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function check if packet is valid (checksum valid)
 *
 * @param  packet       packet to examine (packet header)
 * @param  payload      packet data
 *
 * @return If packet consistency is OK then true is returned, otherwise false.
 */
//==============================================================================
static bool is_packet_payload_valid(const sipc_packet_t *packet, const u8_t *payload)
{
        if (packet->plen == 0) {
                return true;
        }

        if (payload == NULL) {
                return false;
        }

        u16_t payload_checksum = fletcher16(payload, packet->plen);

        if (payload_checksum == packet->payload_checksum) {
                return true;

        } else {
                printk("SIPC: payload checksum: %04Xh:%04Xh->%s", payload_checksum, packet->payload_checksum,
                        "INVALID");
                return false;
        }
}

//==============================================================================
/**
 * @brief  Function return registered socket. Socket list mutex must be locked
 *         by the caller for the whole time the returned socket is used.
 *
 * @param  sipc         stack context
 * @param  port         finding socket with port
 *
 * @return Socket object or NULL.
 */
//==============================================================================
static SIPC_socket_t *find_socket_by_port_locked(sipc_t *sipc, u8_t port)
{
        SIPC_socket_t find_socket;
        find_socket.port = port;

        int pos = sys_llist_find_begin(sipc->socket_list, &find_socket);

        if (pos >= 0) {
                return sys_llist_at(sipc->socket_list, pos);
        }

        return NULL;
}

//==============================================================================
/**
 * @brief  Function handle packet addressed to selected socket. Called with
 *         socket list mutex locked. Function takes ownership of payload.
 *
 * @param  sipc         stack context
 * @param  socket       destination socket
 * @param  packet       packet header
 * @param  payload      packet payload (can be NULL)
 */
//==============================================================================
static void handle_packet(sipc_t *sipc, SIPC_socket_t *socket, const sipc_packet_t *packet, u8_t *payload)
{
        socket->seq = packet->seq;

        u8_t type = packet->type;

        if (type == PACKET_TYPE_DATA) {

                u8_t ptype = PACKET_TYPE_DATAACK;

                if (payload) {
                        int err = sipcbuf__write(socket->rxbuf, payload, packet->plen, false);
                        if (err) {
                                DEBUG("buffer error: %d", err);
                                zfree((void *)&payload);
                                ptype = PACKET_TYPE_DATANACK;

                        } else if (sipcbuf__is_full(socket->rxbuf)) {
                                socket->busy = true;
                                ptype = PACKET_TYPE_DATABUSY;
                        }
                        // on success payload is owned by rxbuf
                }

                send_packet(sipc, packet->seq, packet->port, ptype, NULL, 0);
                return;
        }

        // remaining packet types do not carry data
        if (payload) {
                DEBUG("packet type %u with data - freeing", type);
                zfree((void *)&payload);
        }

        switch (type) {
        case PACKET_TYPE_HANDSHAKEACK:
        case PACKET_TYPE_HANDSHAKENACK:
        case PACKET_TYPE_DATAACK:
        case PACKET_TYPE_DATANACK:
        case PACKET_TYPE_REFUSE:
        case PACKET_TYPE_DATAREPEAT:
                sys_queue_send(socket->ansq, &type, 0);
                break;

        case PACKET_TYPE_DATABUSY:
                // sender waits for ACK
                break;

        case PACKET_TYPE_BIND:
                if (socket->waiting_for_data_ack) {
                        sys_queue_send(socket->ansq, &type, 0);
                }
                break;

        case PACKET_TYPE_HANDSHAKE:
                send_packet(sipc, packet->seq, packet->port, PACKET_TYPE_HANDSHAKEACK, NULL, 0);

                if (socket->waiting_for_data_ack) {
                        sys_queue_send(socket->ansq, &type, 0);
                }
                break;

        default:
                DEBUG("ignoring answer for unknown packet");
                break;
        }
}

//==============================================================================
/**
 * @brief Network interface thread
 *
 * Thread handle receiving of incoming packets.
 *
 * @param arg   thread argument
 */
//==============================================================================
static int input_thread(void *arg)
{
        sipc_t *sipc = arg;

        /* open interface file */
        while (sipc->run and (sipc->if_file == NULL)) {
                sys_sleep_ms(1000);
        }

        while (sipc->run) {
                sipc_packet_t packet;
                u8_t *payload = NULL;

                if (!receive_packet(sipc, &packet, &payload)) {
                        continue; // stack is stopping
                }

                if (sipc->state == NETM_SIPC_STATE__DOWN) {

                        if (payload) {
                                zfree((void *)&payload);
                        }

                        clear_transfer_counters(sipc);

                        if (sys_mutex_lock(sipc->socket_list_mtx, _WAIT_FOREVER_MS) == 0) {

                                sys_llist_foreach (SIPC_socket_t *, socket, sipc->socket_list) {
                                        u8_t type = PACKET_TYPE_REFUSE;
                                        sys_queue_send(socket->ansq, &type, 0);
                                }

                                sys_mutex_unlock(sipc->socket_list_mtx);
                        }

                        continue;
                }

                DEBUG("received packet: seq:%u, port:%d, type:%s, plen:%d", packet.seq, packet.port,
                        packet.type < _PACKET_TYPE_COUNT ? TYPE_STR[packet.type] : "UNKNOWN", packet.plen);

                if (not is_packet_payload_valid(&packet, payload)) {
                        DEBUG("received inconsistent packet");

                        if (payload) {
                                zfree((void *)&payload);
                        }

                        send_packet(sipc, packet.seq, packet.port, PACKET_TYPE_DATAREPEAT, NULL, 0);

                        continue;
                }

                sipc->stats.rx_packets++;
                sipc->stats.rx_bytes += sizeof(sipc_packet_t) + packet.plen;

                /*
                 * Socket list stays locked for the whole packet handling, so
                 * a socket cannot be unregistered/destroyed while in use.
                 */
                if (sys_mutex_lock(sipc->socket_list_mtx, _WAIT_FOREVER_MS) != 0) {
                        if (payload) {
                                zfree((void *)&payload);
                        }
                        continue;
                }

                SIPC_socket_t *socket = find_socket_by_port_locked(sipc, packet.port);

                if (socket) {
                        handle_packet(sipc, socket, &packet, payload);
                        payload = NULL; // ownership passed
                }

                sys_mutex_unlock(sipc->socket_list_mtx);

                if (not socket) {
                        DEBUG("socket for incoming port not registered");

                        if (payload) {
                                zfree((void *)&payload);
                        }

                        send_packet(sipc, packet.seq, packet.port, PACKET_TYPE_REFUSE, NULL, 0);
                }
        }

        return -1;
}

//==============================================================================
/**
 * @brief  Function register selected socket in stack.
 *
 * @param  socket       socket to register
 *
 * @return One of errno value.
 */
//==============================================================================
static int register_socket(sipc_t *sipc, SIPC_socket_t *socket)
{
        int err = sys_mutex_lock(sipc->socket_list_mtx, _WAIT_FOREVER_MS);
        if (!err) {

                if (sys_llist_contains(sipc->socket_list, socket) > 0) {
                        err = EADDRINUSE;

                } else if (sys_llist_push_back(sipc->socket_list, socket) == NULL) {
                        err = ENOMEM;
                }

                sys_mutex_unlock(sipc->socket_list_mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function check if socket is already registered.
 *
 * @param  socket       socket to examine
 *
 * @return If socket is registered then true is returned, otherwise false.
 */
//==============================================================================
static bool is_socket_registered(sipc_t *sipc, SIPC_socket_t *socket)
{
        bool is_registered = false;

        if (sys_mutex_lock(sipc->socket_list_mtx, _WAIT_FOREVER_MS) == 0) {
                is_registered = sys_llist_contains(sipc->socket_list, socket) > 0;
                sys_mutex_unlock(sipc->socket_list_mtx);
        }

        return is_registered;
}

//==============================================================================
/**
 * @brief  Function unregister selected socket from stack.
 *
 * @param  socket       socket to unregister
 */
//==============================================================================
static void unregister_socket(sipc_t *sipc, SIPC_socket_t *socket)
{
        int err = sys_mutex_lock(sipc->socket_list_mtx, _WAIT_FOREVER_MS);
        if (!err) {
                int pos = sys_llist_find_begin(sipc->socket_list, socket);

                if (pos >= 0) {
                        sys_llist_take(sipc->socket_list, pos);
                }

                sys_mutex_unlock(sipc->socket_list_mtx);
        }
}

//==============================================================================
/**
 * @brief  Function initialize network.
 * @param  ctx          context
 * @param  if_path      interface path
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_ifinit(void **ctx, const char *if_path)
{
        if (!ctx || !if_path) {
                return EINVAL;
        }

        int err = zalloc(sizeof(sipc_t), ctx);
        if (!err) {
                sipc_t *sipc = *ctx;

                static const _thread_attr_t attr = {.priority = _PRIORITY_NORMAL,
                        .stack_depth = _STACK_DEPTH_LOW,
                        .detached = true};

                sipc->run = true;

                err = sys_fopen(if_path, O_RDWR, 0, &sipc->if_file);

                if (!err) {
                        err = sys_mutex_create(KMTX_TYPE_NORMAL, &sipc->socket_list_mtx);
                }

                if (!err) {
                        err = sys_mutex_create(KMTX_TYPE_NORMAL, &sipc->packet_send_mtx);
                }

                if (!err) {
                        err = _llist_create_krn(_MM_NET, list_cmp_functor, NULL, &sipc->socket_list);
                }

                // thread is started only when all resources are ready
                if (!err) {
                        err = sys_thread_create(input_thread, &attr, sipc, &sipc->if_thread);
                }

                if (err) {
                        printk("SIPC: stack initialization error (%d)", err);
                        SIPC_ifdeinit(sipc);
                        *ctx = NULL;
                } else {
                        printk("SIPC (%s): thread ID: %u", if_path, sipc->if_thread);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function deinitialize network.
 * @param  ctx   context
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_ifdeinit(void *ctx)
{
        sipc_t *sipc = ctx;

        if (!sipc) {
                return EINVAL;
        }

        sipc->run = false;

        if (sipc->if_thread) {
                sys_thread_destroy(sipc->if_thread);
                sipc->if_thread = 0;
        }

        if (sipc->socket_list_mtx) {
                sys_mutex_destroy(sipc->socket_list_mtx);
                sipc->socket_list_mtx = NULL;
        }

        if (sipc->packet_send_mtx) {
                sys_mutex_destroy(sipc->packet_send_mtx);
                sipc->packet_send_mtx = NULL;
        }

        if (sipc->socket_list) {
                // NOTE: sockets are owned by the caller - list must not free them
                sys_llist_destroy(sipc->socket_list);
                sipc->socket_list = NULL;
        }

        if (sipc->if_file) {
                sys_fclose(sipc->if_file);
                sipc->if_file = NULL;
        }

        return zfree(&ctx);
}

//==============================================================================
/**
 * @brief  Function up the network.
 * @param  cfg   configuration structure
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_ifup(void *ctx, const NETM_SIPC_config_t *cfg, size_t cfg_size)
{
        sipc_t *sipc = ctx;

        int err = EINVAL;

        if (sipc && cfg && (cfg_size >= sizeof(*cfg))) {
                sipc->conf.MTU = max(DEFAULT_MAX_PAYLOAD, cfg->MTU);
                sipc->state = NETM_SIPC_STATE__UP;
                err = 0;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function turn down interface with static configuration.
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_ifdown(void *ctx)
{
        sipc_t *sipc = ctx;

        int err = ENONET;

        if ((sipc != NULL) && (sipc->state == NETM_SIPC_STATE__UP)) {

                sipc->state = NETM_SIPC_STATE__DOWN;

                clear_transfer_counters(sipc);

                err = ESUCC;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function returns interface status.
 * @param  status       status container
 * @param  status_size  status size (at least)
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_ifstatus(void *ctx, NETM_SIPC_status_t *status, size_t status_size)
{
        sipc_t *sipc = ctx;

        if (!status or (status_size < sizeof(*status))) {
                return EINVAL;
        }

        if (sipc) {
                status->MTU = sipc->conf.MTU;
                status->rx_packets = sipc->stats.rx_packets;
                status->rx_bytes = sipc->stats.rx_bytes;
                status->tx_packets = sipc->stats.tx_packets;
                status->tx_bytes = sipc->stats.tx_bytes;
                status->state = sipc->state;

                return ESUCC;
        } else {
                return ENONET;
        }
}

//==============================================================================
/**
 * @brief  Function create socket container.
 * @param  type         socket type
 * @param  prot         protocol
 * @param  socket       sipc socket (object is already created)
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_create(void *ctx, NETM_socket_type_t type, u8_t prot, SIPC_socket_t *socket)
{
        UNUSED_ARG2(ctx, prot);

        if (!socket) {
                return EINVAL;
        }

        int err = EINVAL;

        if (type == NETM_SOCKET_TYPE__STREAM) {

                socket->port = 0;
                socket->seq = 0;
                socket->busy = false;
                socket->waiting_for_data_ack = false;
                socket->recv_timeout = _WAIT_FOREVER_MS;
                socket->send_timeout = _WAIT_FOREVER_MS;
                socket->rxbuf = NULL;
                socket->ansq = NULL;

                err = sipcbuf__create((void *)&socket->rxbuf, __NETWORK_SIPC_RECV_BUF_SIZE__);

                if (!err) {
                        err = sys_queue_create(1, sizeof(((sipc_packet_t *)NULL)->type), &socket->ansq);
                }

                if (err) {
                        if (socket->rxbuf) {
                                sipcbuf__destroy(socket->rxbuf);
                                socket->rxbuf = NULL;
                        }

                        if (socket->ansq) {
                                sys_queue_destroy(socket->ansq);
                                socket->ansq = NULL;
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function destroy created socket.
 * @param  socket    sipc socket to destroy
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_destroy(void *ctx, SIPC_socket_t *socket)
{
        sipc_t *sipc = ctx;

        if (!socket) {
                return EINVAL;
        }

        /*
         * After unregistration input thread cannot find the socket anymore,
         * because packets are handled with socket list locked.
         */
        if (sipc && socket->port) {
                unregister_socket(sipc, socket);
        }

        socket->port = 0;
        socket->recv_timeout = 0;
        socket->send_timeout = 0;

        if (socket->rxbuf) {
                sipcbuf__destroy(socket->rxbuf);
                socket->rxbuf = NULL;
        }

        int err = ESUCC;

        if (socket->ansq) {
                err = sys_queue_destroy(socket->ansq);
                socket->ansq = NULL;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function connect socket to selected address.
 * @param  socket    socket
 * @param  addr      sipc address
 * @param  addr_size sipc address size
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_connect(void *ctx, SIPC_socket_t *socket, const NETM_SIPC_sockaddr_t *addr, size_t addr_size)
{
        sipc_t *sipc = ctx;

        if (!sipc || !socket || !addr || (addr->port == 0) || (addr_size != sizeof(*addr))) {
                return EINVAL;
        }

        // socket already connected or bound - prevent double registration
        if (socket->port != 0) {
                return EADDRINUSE;
        }

        socket->port = addr->port;

        int err = register_socket(sipc, socket);
        if (err) {
                socket->port = 0;
                return err;
        }

        sys_queue_reset(socket->ansq);

        socket->seq = sipc->seq_ctr;

        err = send_packet(sipc, socket->seq, socket->port, PACKET_TYPE_HANDSHAKE, NULL, 0);
        if (!err) {
                u8_t type;
                err = sys_queue_receive(socket->ansq, &type, CONNECTION_TIMEOUT);
                if (!err) {
                        switch (type) {
                        case PACKET_TYPE_HANDSHAKEACK:
                                break;

                        case PACKET_TYPE_HANDSHAKENACK:
                        case PACKET_TYPE_REFUSE:
                                err = ECONNREFUSED;
                                break;

                        default:
                                err = EFAULT;
                                break;
                        }
                } else {
                        DEBUG("connection timeout");
                }
        }

        if (err) {
                unregister_socket(sipc, socket);
                socket->port = 0;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function disconnect socket from selected address.
 * @param  socket     socket
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_disconnect(void *ctx, SIPC_socket_t *socket)
{
        sipc_t *sipc = ctx;

        if (!sipc || !socket) {
                return EINVAL;
        }

        unregister_socket(sipc, socket);
        sipcbuf__clear(socket->rxbuf);
        sys_queue_reset(socket->ansq);
        socket->port = 0;
        socket->busy = false;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function shutdown selected socket direction.
 * @param  socket    socket
 * @param  how          shutdown direction
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_shutdown(void *ctx, SIPC_socket_t *socket, NETM_shut_t how)
{
        UNUSED_ARG3(ctx, socket, how);
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief  Function bind selected address with socket.
 * @param  socket     socket
 * @param  addr          inet address
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_bind(void *ctx, SIPC_socket_t *socket, const NETM_SIPC_sockaddr_t *addr, size_t addr_size)
{
        sipc_t *sipc = ctx;

        if (!sipc || !socket || !addr || (addr->port == 0) || (addr_size != sizeof(*addr))) {
                return EINVAL;
        }

        // socket already connected or bound - prevent double registration
        if (socket->port != 0) {
                return EADDRINUSE;
        }

        socket->port = addr->port;

        int err = register_socket(sipc, socket);
        if (!err) {

                sys_queue_reset(socket->ansq);
                socket->seq = sipc->seq_ctr;
                err = send_packet(sipc, socket->seq, socket->port, PACKET_TYPE_BIND, NULL, 0);

        } else {
                socket->port = 0;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function listen connection.
 * @param  socket        socket
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_listen(void *ctx, SIPC_socket_t *socket)
{
        UNUSED_ARG2(ctx, socket);
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief  Function accept incoming connection.
 * @param  socket       socket
 * @param  new_socket   new socket of accepted connection
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_accept(void *ctx, SIPC_socket_t *socket, SIPC_socket_t *new_socket)
{
        UNUSED_ARG3(ctx, socket, new_socket);
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief  Function release remote sender when buffer is not full anymore.
 * @param  sipc         stack context
 * @param  socket       socket
 */
//==============================================================================
static void release_busy_state(sipc_t *sipc, SIPC_socket_t *socket)
{
        if (socket->busy && !sipcbuf__is_full(socket->rxbuf)) {
                socket->busy = false;
                send_packet(sipc, socket->seq, socket->port, PACKET_TYPE_DATAACK, NULL, 0);
        }
}

//==============================================================================
/**
 * @brief  Function receive incoming data.
 * @param  socket    socket
 * @param  buf          buffer for data
 * @param  len          number of data to receive
 * @param  flags        flags
 * @param  recved       number of received bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_recv(void *ctx, SIPC_socket_t *socket, void *buf, size_t len, NETM_flags_t flags, size_t *recved)
{
        sipc_t *sipc = ctx;

        if (!sipc || !socket || !buf || !recved) {
                return EINVAL;
        }

        *recved = 0;

        if (!is_socket_registered(sipc, socket)) {
                return ECONNREFUSED;
        }

        int err = ETIME;
        clock_t tref = sys_get_uptime_ms();

        while (!sys_is_time_expired(tref, socket->recv_timeout)) {

                err = sipcbuf__read(socket->rxbuf, buf, len, recved);
                if (!err) {
                        if (*recved == 0) {
                                err = ETIME;
                                sys_sleep_ms(5);
                                continue;
                        } else {
                                release_busy_state(sipc, socket);
                        }
                }

                break;
        }

        if (flags & NETM_FLAGS__FREEBUF) {
                sipcbuf__clear(socket->rxbuf);

                // buffer is empty now - remote sender must be released
                release_busy_state(sipc, socket);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function receive data from selected address.
 * @param  socket    socket
 * @param  buf          buffer for data
 * @param  len          number of bytes to receive
 * @param  flags        flags
 * @param  sockaddr     socket address
 * @param  recved       number of received bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_recvfrom(void *ctx, SIPC_socket_t *socket, void *buf, size_t len, NETM_flags_t flags,
        NETM_SIPC_sockaddr_t *sockaddr, size_t addr_size, size_t *recved)
{
        UNUSED_ARG8(ctx, socket, buf, len, flags, sockaddr, addr_size, recved);
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief  Function send data to connected socket.
 * @param  socket    socket
 * @param  buf          buffer to send
 * @param  len          number of bytes to send
 * @param  flags        flags
 * @param  sent         number of sent bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_send(void *ctx, SIPC_socket_t *socket, const void *buf, size_t len, NETM_flags_t flags, size_t *sent)
{
        UNUSED_ARG1(flags);

        sipc_t *sipc = ctx;

        if (!sipc || !socket || !buf || !sent) {
                return EINVAL;
        }

        *sent = 0;

        if (!is_socket_registered(sipc, socket)) {
                return ECONNREFUSED;
        }

        const u8_t *data = buf;
        int err = EINVAL;
        int repeat = 0;

        while (len) {

                u16_t plen = min(len, sipc->conf.MTU);
                if (plen == 0) {
                        err = ENONET; // interface not configured
                        break;
                }

                // drop stale answers before sending next chunk
                sys_queue_reset(socket->ansq);

                socket->seq = sipc->seq_ctr;
                socket->waiting_for_data_ack = true;

                err = send_packet(sipc, socket->seq, socket->port, PACKET_TYPE_DATA, data, plen);
                if (err) {
                        break;
                }

                u8_t type;
                err = sys_queue_receive(socket->ansq, &type, socket->send_timeout);
                if (err) {
                        break;
                }

                switch (type) {
                case PACKET_TYPE_DATAACK:
                        data += plen;
                        *sent += plen;
                        len -= plen;
                        repeat = 0;
                        break;

                case PACKET_TYPE_DATANACK:
                case PACKET_TYPE_REFUSE:
                        err = ECONNREFUSED;
                        len = 0;
                        break;

                case PACKET_TYPE_DATAREPEAT:
                        printk("SIPC: data repeat requested");
                        if (++repeat > MAX_DATA_REPEAT) {
                                err = EIO;
                                len = 0;
                        }
                        break;

                case PACKET_TYPE_BIND:
                case PACKET_TYPE_HANDSHAKE:
                case PACKET_TYPE_HANDSHAKEACK:
                case PACKET_TYPE_HANDSHAKENACK:
                        err = ECONNRESET;
                        len = 0;
                        break;

                default:
                        err = EFAULT;
                        len = 0;
                        break;
                }
        }

        socket->waiting_for_data_ack = false;

        return err;
}

//==============================================================================
/**
 * @brief  Function send buffer to selected address.
 * @param  socket    socket
 * @param  buf          buffer to send
 * @param  len          number of bytes to send
 * @param  flags        flags
 * @param  to_sockaddr  socket address
 * @param  sent         number of sent bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_sendto(void *ctx, SIPC_socket_t *socket, const void *buf, size_t len, NETM_flags_t flags,
        const NETM_SIPC_sockaddr_t *to_addr, size_t to_addr_size, size_t *sent)
{
        UNUSED_ARG8(ctx, socket, buf, len, flags, to_addr, to_addr_size, sent);
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief  Function gets host address by name.
 * @param  name         address name
 * @param  addr         received address
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_gethostbyname(void *ctx, const char *name, NETM_SIPC_sockaddr_t *addr, size_t addr_size)
{
        UNUSED_ARG4(ctx, name, addr, addr_size);

        return ENOTSUP;
}

//==============================================================================
/**
 * @brief  Function set receive timeout.
 * @param  socket    socket
 * @param  timeout      timeout value in milliseconds
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_set_recv_timeout(void *ctx, SIPC_socket_t *socket, uint32_t timeout)
{
        UNUSED_ARG1(ctx);

        if (!socket) {
                return EINVAL;
        }

        socket->recv_timeout = timeout;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function set send timeout.
 * @param  socket    socket
 * @param  timeout      timeout value in milliseconds
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_set_send_timeout(void *ctx, SIPC_socket_t *socket, uint32_t timeout)
{
        UNUSED_ARG1(ctx);

        if (!socket) {
                return EINVAL;
        }

        socket->send_timeout = timeout;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function get receive timeout.
 * @param  socket    socket
 * @param  timeout      timeout value in milliseconds
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_get_recv_timeout(void *ctx, SIPC_socket_t *socket, uint32_t *timeout)
{
        UNUSED_ARG1(ctx);

        if (!socket || !timeout) {
                return EINVAL;
        }

        *timeout = socket->recv_timeout;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function get send timeout.
 * @param  socket    socket
 * @param  timeout      timeout value in milliseconds
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_get_send_timeout(void *ctx, SIPC_socket_t *socket, uint32_t *timeout)
{
        UNUSED_ARG1(ctx);

        if (!socket || !timeout) {
                return EINVAL;
        }

        *timeout = socket->send_timeout;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function returns address of socket (remote connection address).
 * @param  socket    socket
 * @param  sockaddr     socket address (address and port)
 * @return One of @ref errno value.
 */
//==============================================================================
int SIPC_socket_getaddress(void *ctx, SIPC_socket_t *socket, NETM_SIPC_sockaddr_t *addr, size_t addr_size)
{
        UNUSED_ARG1(ctx);

        if (!socket || !addr || (addr_size < sizeof(*addr))) {
                return EINVAL;
        }

        addr->port = socket->port;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function convert value for host/network purpose.
 * @param  value        value to convert
 * @return Converted value.
 */
//==============================================================================
u16_t SIPC_hton_u16(u16_t value)
{
#if _CPUCTL_BYTE_ORDER == _BYTE_ORDER_LITTLE_ENDIAN
        return value;
#elif _CPUCTL_BYTE_ORDER == _BYTE_ORDER_BIG_ENDIAN
        return ((value & 0x00FF) << 8) | ((value & 0xFF00) >> 8);
#else
#error "Not supported endianness!"
#endif
}

//==============================================================================
/**
 * @brief  Function convert value for host/network purpose.
 * @param  value        value to convert
 * @return Converted value.
 */
//==============================================================================
u32_t SIPC_hton_u32(u32_t value)
{
#if _CPUCTL_BYTE_ORDER == _BYTE_ORDER_LITTLE_ENDIAN
        return value;
#elif _CPUCTL_BYTE_ORDER == _BYTE_ORDER_BIG_ENDIAN
        return ((value & 0x000000FFUL) << 24) | ((value & 0x0000FF00UL) << 8) | ((value & 0x00FF0000UL) >> 8) |
                ((value & 0xFF000000UL) >> 24);
#else
#error "Not supported endianness!"
#endif
}

//==============================================================================
/**
 * @brief  Function convert value for host/network purpose.
 * @param  value        value to convert
 * @return Converted value.
 */
//==============================================================================
u64_t SIPC_hton_u64(u64_t value)
{
#if _CPUCTL_BYTE_ORDER == _BYTE_ORDER_LITTLE_ENDIAN
        return value;
#elif _CPUCTL_BYTE_ORDER == _BYTE_ORDER_BIG_ENDIAN
        return ((value & 0x00000000000000FFULL) << 56) | ((value & 0x000000000000FF00ULL) << 40) |
                ((value & 0x0000000000FF0000ULL) << 24) | ((value & 0x00000000FF000000ULL) << 8) |
                ((value & 0x000000FF00000000ULL) >> 8) | ((value & 0x0000FF0000000000ULL) >> 24) |
                ((value & 0x00FF000000000000ULL) >> 40) | ((value & 0xFF00000000000000ULL) >> 56);
#else
#error "Not supported endianness!"
#endif
}

/*==============================================================================
  End of file
==============================================================================*/
