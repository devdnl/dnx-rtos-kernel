/*==============================================================================
File     uds.c

Author   Daniel Zorychta

Brief    Unix Domain Socket Network management.

         Copyright (C) 2024 Daniel Zorychta <daniel.zorychta@gmail.com>

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

// #pragma GCC optimize("Og")

/*==============================================================================
  Include files
==============================================================================*/
#include <stdbool.h>
#include "net/uds/uds.h"
#include "cpuctl.h"
#include "lib/llist.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define zalloc(_size, _pptr) \
        _kzalloc(_MM_NET, _size, NULL, _MM_FLAG__DMA, _MM_FLAG__DMA, _pptr)

#define kalloc(_size, _pptr) \
        _kmalloc(_MM_NET, _size, NULL, _MM_FLAG__DMA, _MM_FLAG__DMA, _pptr)

#define zfree(_pptr) _kfree(_MM_NET, _pptr)

#if __NETWORK_UDS_DEBUG_ON__ > 0
#define DEBUG(...) printk("UDS: "__VA_ARGS__)
#else
#define DEBUG(...)
#endif

#define STREAM_BUF_SIZE      __NETWORK_UDS_BUF_SIZE__
#define MAX_DATAGRAM_BUFFERS __NETWORK_UDS_DATAGRAM_BUFFERS_SIZE__
#define MAX_DATAGRAM_SIZE    __NETWORK_UDS_DATAGRAM_MAX_SIZE__

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        size_t len;
        char  *from;
        u8_t  *data;
} datagram_t;

typedef struct {
        struct {
                kmtx_t  *mutex;
                llist_t *list;
        } socket;

        struct {
                u64_t rx_packets;
                u64_t tx_packets;
                u64_t rx_bytes;
                u64_t tx_bytes;
        } stats;

        NETM_UDS_state_t state;
} uds_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  External function prototypes
==============================================================================*/
static int socket_register(uds_t *uds, UDS_socket_t *socket);
static int socket_unregister(uds_t *uds, UDS_socket_t *socket);
static int buffer_create(uds_t *uds, UDS_socket_t *socket);
static int buffer_reset(uds_t *uds, UDS_socket_t *socket);
static int buffer_datagram_receive(uds_t *uds, UDS_socket_t *socket, void *buf,
        size_t len, NETM_UDS_sockaddr_t *addr, size_t *recved);
static int buffer_datagram_send(uds_t *uds, UDS_socket_t *socket, kqueue_t *buffer,
        const void *data, size_t len, size_t *sent);
static int buffer_stream_receive(uds_t *uds, UDS_socket_t *socket, void *buf, size_t len,
        size_t *recved);
static int buffer_stream_send(uds_t *uds, UDS_socket_t *socket, kqueue_t *buffer,
        const void *buf, size_t len, size_t *sent);

/*==============================================================================
  Local objects
==============================================================================*/

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  External objects
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Function initialize network.
 * @param  ctx          context
 * @param  if_path      interface path
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_ifinit(void **ctx, const char *if_path)
{
        UNUSED_ARG1(if_path);

        int err = zalloc(sizeof(uds_t), ctx);
        if (err) {
                return err;
        }

        uds_t *uds = *ctx;

        err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &uds->socket.mutex);
        if (err) {
                UDS_ifdeinit(*ctx);
                return err;
        }

        err = _llist_create_krn(_MM_NET, sys_llist_functor_cmp_pointers, NULL,
                &uds->socket.list);
        if (err) {
                UDS_ifdeinit(*ctx);
                return err;
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
int UDS_ifdeinit(void *ctx)
{
        uds_t *uds = ctx;

        if (sys_llist_size(uds->socket.list) > 0) {
                DEBUG("%s: stack in use.");
                return EBUSY;
        }

        if (uds->socket.mutex) {
                sys_mutex_destroy(uds->socket.mutex);
        }

        if (uds->socket.list) {
                sys_llist_destroy(uds->socket.list);
        }

        return zfree(&ctx);
}

//==============================================================================
/**
 * @brief  Function up the network.
 * @param  cfg          configuration structure
 * @param  cfg_size     configuration size
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_ifup(void *ctx, const NETM_UDS_config_t *cfg, size_t cfg_size)
{
        UNUSED_ARG2(cfg, cfg_size);

        uds_t *uds = ctx;
        uds->state = NETM_UDS_STATE__UP;
        return 0;
}

//==============================================================================
/**
 * @brief  Function turn down interface with static configuration.
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_ifdown(void *ctx)
{
        uds_t *uds = ctx;
        uds->state = NETM_UDS_STATE__DOWN;
        return 0;
}

//==============================================================================
/**
 * @brief  Function returns interface status.
 * @param  status       status container
 * @param  status_size  status size (at least)
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_ifstatus(void *ctx, NETM_UDS_status_t *status, size_t status_size)
{
        uds_t *uds = ctx;

        if (status_size < sizeof(*status)) {
                return EINVAL;
        }

        if (uds) {
                status->rx_packets = uds->stats.rx_packets;
                status->rx_bytes   = uds->stats.rx_bytes;
                status->tx_packets = uds->stats.tx_packets;
                status->tx_bytes   = uds->stats.tx_bytes;
                status->state      = uds->state;

                return ESUCC;
        } else {
                return ENONET;
        }
}

//==============================================================================
/**
 * @brief  Function create socket container.
 * @param  prot         protocol
 * @param  socket       uds socket (object is already created)
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_create(void *ctx, NETM_protocol_t prot, UDS_socket_t *socket)
{
        uds_t *uds = ctx;

        socket->path         = NULL;
        socket->protocol     = prot;
        socket->recv_timeout = _MAX_DELAY_MS;
        socket->send_timeout = _MAX_DELAY_MS;
        socket->buf          = NULL;
        socket->shutdown     = 0;
        socket->conn_req     = NULL;
        socket->conn_sock    = NULL;
        socket->connected    = false;

        return socket_register(uds, socket);
}

//==============================================================================
/**
 * @brief  Function destroy created socket.
 * @param  socket    uds socket to destroy
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_destroy(void *ctx, UDS_socket_t *socket)
{
        uds_t *uds = ctx;

        int err = socket_unregister(uds, socket);
        if (err) {
                return err;
        }

        if (socket->path) {
                zfree(cast(void **, &socket->path));
        }

        if (socket->buf) {
                buffer_reset(uds, socket);
                sys_queue_destroy(socket->buf);
        }

        if (socket->conn_req) {
                sys_queue_destroy(socket->conn_req);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function connect socket to selected address.
 * @param  socket       socket
 * @param  addr      uds address
 * @param  addr_size uds address size
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_connect(void *ctx, UDS_socket_t *socket, const NETM_UDS_sockaddr_t *addr,
        size_t addr_size)
{
        uds_t *uds = ctx;

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        if (addr_size != sizeof(*addr)) {
                return EINVAL;
        }

        int err = buffer_create(uds, socket);
        if (err) {
                return err;
        }

        err = sys_mutex_lock(uds->socket.mutex, _MAX_DELAY_MS);
        if (not err) {
                err = ENOENT;

                sys_llist_foreach (UDS_socket_t *, s, uds->socket.list) {
                        if (s->path and s->conn_req and isstreq(s->path, addr->path)) {
                                err = sys_queue_send(s->conn_req, &socket,
                                        socket->recv_timeout);
                                break;
                        }
                }

                sys_mutex_unlock(uds->socket.mutex);
        }

        clock_t tref = sys_get_uptime_ms();

        while (not err) {
                if (socket->conn_sock) {
                        socket->connected = true;
                        break;

                } else if (sys_get_uptime_ms() - tref >= socket->recv_timeout) {
                        err = ETIME;
                        break;

                } else {
                        sys_sleep_ms(100);
                }
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
int UDS_socket_disconnect(void *ctx, UDS_socket_t *socket)
{
        uds_t *uds = ctx;

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        if (not socket->conn_sock) {
                return ENOENT;
        }

        socket->conn_sock->connected = false;
        socket->connected            = false;

        return 0;
}

//==============================================================================
/**
 * @brief  Function shutdown selected socket direction.
 * @param  socket       socket
 * @param  how          shutdown direction
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_shutdown(void *ctx, UDS_socket_t *socket, NETM_shut_t how)
{
        UNUSED_ARG1(ctx);

        socket->shutdown = how;

        return 0;
}

//==============================================================================
/**
 * @brief  Function bind selected address with socket.
 * @param  socket       socket
 * @param  addr         inet address
 * @param  addr_size    inet address size
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_bind(void *ctx, UDS_socket_t *socket, const NETM_UDS_sockaddr_t *addr,
        size_t addr_size)
{
        uds_t *uds = ctx;

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        if (addr_size != sizeof(*addr)) {
                return EINVAL;
        }

        size_t path_len = strnlen(addr->path, sizeof(addr->path));
        if (path_len == sizeof(addr->path)) {
                return EINVAL;
        }

        int err = sys_mutex_lock(uds->socket.mutex, _MAX_DELAY_MS);
        if (not err) {
                sys_llist_foreach (UDS_socket_t *, s, uds->socket.list) {
                        if (s->path and isstreq(s->path, addr->path)) {
                                err = EADDRINUSE;
                                break;
                        }
                }

                if (not err) {
                        if (socket->path) {
                                zfree(cast(void **, &socket->path));
                        }

                        err = kalloc(path_len + 1, cast(void **, &socket->path));
                        if (err) {
                                return err;
                        }

                        sys_strlcpy(socket->path, addr->path, path_len + 1);
                }

                sys_mutex_unlock(uds->socket.mutex);
        }

        if (not err and (socket->protocol == NETM_PROTOCOL__DATAGRAM)) {
                err = buffer_create(uds, socket);
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
int UDS_socket_listen(void *ctx, UDS_socket_t *socket)
{
        uds_t *uds = ctx;

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        int err = 0;

        if (not socket->conn_req) {
                err = sys_queue_create(8, sizeof(UDS_socket_t *), &socket->conn_req);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function accept incoming connection.
 * @param  socket       socket
 * @param  new_socket   new socket of accepted connection
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_accept(void *ctx, UDS_socket_t *socket, UDS_socket_t *new_socket)
{
        uds_t *uds = ctx;

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        UDS_socket_t *req_socket;
        int err = sys_queue_receive(socket->conn_req, &req_socket, socket->recv_timeout);
        if (err) {
                return err;
        }

        if (req_socket == NULL) {
                return EAGAIN;
        }

        if (req_socket->protocol != socket->protocol) {
                return ECONNABORTED;
        }

        err = UDS_socket_create(uds, socket->protocol, new_socket);
        if (err) {
                return err;
        }

        err = buffer_create(uds, new_socket);
        if (err) {
                return err;
        }

        new_socket->conn_sock = req_socket;
        req_socket->conn_sock = new_socket;
        new_socket->connected = true;

        return 0;
}

//==============================================================================
/**
 * @brief  Function receive incoming data.
 * @param  socket       socket
 * @param  buf          buffer for data
 * @param  len          number of data to receive
 * @param  flags        flags
 * @param  recved       number of received bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_recv(void *ctx, UDS_socket_t *socket, void *buf, size_t len,
        NETM_flags_t flags, size_t *recved)
{
        uds_t *uds = ctx;

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        if (socket->shutdown & NETM_SHUT__RD) {
                return EPERM;
        }

        if (not socket->conn_sock or not socket->connected) {
                return ECONNREFUSED;
        }

        int err = EINVAL;

        if (socket->protocol == NETM_PROTOCOL__STREAM) {
                err = buffer_stream_receive(uds, socket, buf, len, recved);

        } else if (socket->protocol == NETM_PROTOCOL__DATAGRAM) {
                err = buffer_datagram_receive(uds, socket, buf, len, NULL, recved);
        }

        if (flags & NETM_FLAGS__FREEBUF) {
                buffer_reset(uds, socket);
        }

        uds->stats.rx_bytes += *recved;
        uds->stats.rx_packets++;

        return err;
}

//==============================================================================
/**
 * @brief  Function receive data from selected address.
 * @param  socket       socket
 * @param  buf          buffer for data
 * @param  len          number of bytes to receive
 * @param  flags        flags
 * @param  addr         socket address
 * @param  addr_size    socket address size
 * @param  recved       number of received bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_recvfrom(void *ctx, UDS_socket_t *socket, void *buf, size_t len,
        NETM_flags_t flags, NETM_UDS_sockaddr_t *addr, size_t addr_size, size_t *recved)
{
        uds_t *uds = ctx;

        if (addr_size != sizeof(*addr)) {
                return EINVAL;
        }

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        if (socket->protocol == NETM_PROTOCOL__STREAM) {
                return ECANCELED;
        }

        if (socket->shutdown & NETM_SHUT__RD) {
                return EPERM;
        }

        int err = buffer_datagram_receive(uds, socket, buf, len, addr, recved);

        if (flags & NETM_FLAGS__FREEBUF) {
                buffer_reset(uds, socket);
        }

        uds->stats.rx_bytes += *recved;
        uds->stats.rx_packets++;

        return err;
}

//==============================================================================
/**
 * @brief  Function send data to connected socket.
 * @param  socket       socket
 * @param  buf          buffer to send
 * @param  len          number of bytes to send
 * @param  flags        flags
 * @param  sent         number of sent bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_send(void *ctx, UDS_socket_t *socket, const void *buf, size_t len,
        NETM_flags_t flags, size_t *sent)
{
        UNUSED_ARG1(flags);

        uds_t *uds = ctx;

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        if (socket->shutdown & NETM_SHUT__WR) {
                return EPERM;
        }

        if (not socket->conn_sock or not socket->connected) {
                return ECONNREFUSED;
        }

        int err = EINVAL;

        if (socket->protocol == NETM_PROTOCOL__STREAM) {
                err = buffer_stream_send(uds, socket, socket->conn_sock->buf, buf, len,
                        sent);

        } else if (socket->protocol == NETM_PROTOCOL__DATAGRAM) {
                err = buffer_datagram_send(uds, socket, socket->conn_sock->buf, buf, len,
                        sent);
        }

        uds->stats.tx_bytes += *sent;
        uds->stats.tx_packets++;

        return err;
}

//==============================================================================
/**
 * @brief  Function send buffer to selected address.
 * @param  socket       socket
 * @param  buf          buffer to send
 * @param  len          number of bytes to send
 * @param  flags        flags
 * @param  to_addr      socket address
 * @param  to_addr_size socket address size
 * @param  sent         number of sent bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_sendto(void *ctx, UDS_socket_t *socket, const void *buf, size_t len,
        NETM_flags_t flags, const NETM_UDS_sockaddr_t *to_addr, size_t to_addr_size,
        size_t *sent)
{
        UNUSED_ARG1(flags);

        uds_t *uds = ctx;

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        if (to_addr_size != sizeof(*to_addr)) {
                return EINVAL;
        }

        if (socket->protocol == NETM_PROTOCOL__STREAM) {
                return ECANCELED;
        }

        if (socket->shutdown & NETM_SHUT__WR) {
                return EPERM;
        }

        UDS_socket_t *to_socket = NULL;

        int err = sys_mutex_lock(uds->socket.mutex, _MAX_DELAY_MS);
        if (not err) {
                err = ENOENT;

                sys_llist_foreach (UDS_socket_t *, s, uds->socket.list) {
                        if (s->path and isstreq(s->path, to_addr->path)) {
                                to_socket = s;
                                err       = 0;
                                break;
                        }
                }

                sys_mutex_unlock(uds->socket.mutex);
        }

        if (err) {
                return err;
        }

        if (to_socket->protocol != socket->protocol) {
                return ECONNABORTED;
        }

        err = buffer_datagram_send(uds, socket, to_socket->buf, buf, len, sent);

        uds->stats.tx_bytes += *sent;
        uds->stats.tx_packets++;

        return err;
}

//==============================================================================
/**
 * @brief  Function gets host address by name.
 * @param  name         address name
 * @param  addr         received address
 * @param  addr_size    address size
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_gethostbyname(void *ctx, const char *name, NETM_UDS_sockaddr_t *addr,
        size_t addr_size)
{
        UNUSED_ARG4(ctx, name, addr, addr_size);

        return ENOTSUP;
}

//==============================================================================
/**
 * @brief  Function set receive timeout.
 * @param  socket       socket
 * @param  timeout      timeout value in milliseconds
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_set_recv_timeout(void *ctx, UDS_socket_t *socket, uint32_t timeout)
{
        UNUSED_ARG1(ctx);

        socket->recv_timeout = timeout;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function set send timeout.
 * @param  socket       socket
 * @param  timeout      timeout value in milliseconds
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_set_send_timeout(void *ctx, UDS_socket_t *socket, uint32_t timeout)
{
        UNUSED_ARG1(ctx);

        socket->send_timeout = timeout;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function get receive timeout.
 * @param  socket       socket
 * @param  timeout      timeout value in milliseconds
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_get_recv_timeout(void *ctx, UDS_socket_t *socket, uint32_t *timeout)
{
        UNUSED_ARG1(ctx);

        *timeout = socket->recv_timeout;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function get send timeout.
 * @param  socket       socket
 * @param  timeout      timeout value in milliseconds
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_get_send_timeout(void *ctx, UDS_socket_t *socket, uint32_t *timeout)
{
        UNUSED_ARG1(ctx);

        *timeout = socket->send_timeout;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function returns address of socket (remote connection address).
 * @param  socket    socket
 * @param  addr      socket address (address and port)
 * @param  addr_size address size
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_getaddress(void *ctx, UDS_socket_t *socket, NETM_UDS_sockaddr_t *addr,
        size_t addr_size)
{
        UNUSED_ARG1(ctx);

        if (addr_size != sizeof(*addr)) {
                return EINVAL;
        }

        if (socket->conn_sock and socket->connected) {
                sys_strlcpy(addr->path, socket->conn_sock->path, sizeof(addr->path));
                return 0;

        } else if (socket->path) {
                sys_strlcpy(addr->path, socket->path, sizeof(addr->path));
                return 0;

        } else {
                sys_strlcpy(addr->path, "", sizeof(addr->path));
                return 0;
        }
}

//==============================================================================
/**
 * @brief  Function convert value for host/network purpose.
 * @param  value        value to convert
 * @return Converted value.
 */
//==============================================================================
u16_t UDS_hton_u16(u16_t value)
{
        return value;
}

//==============================================================================
/**
 * @brief  Function convert value for host/network purpose.
 * @param  value        value to convert
 * @return Converted value.
 */
//==============================================================================
u32_t UDS_hton_u32(u32_t value)
{
        return value;
}

//==============================================================================
/**
 * @brief  Function convert value for host/network purpose.
 * @param  value        value to convert
 * @return Converted value.
 */
//==============================================================================
u64_t UDS_hton_u64(u64_t value)
{
        return value;
}

//==============================================================================
/**
 * @brief  Register socket.
 *
 * @param  uds          UDS instance
 * @param  socket       socket
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int socket_register(uds_t *uds, UDS_socket_t *socket)
{
        int err = sys_mutex_lock(uds->socket.mutex, _MAX_DELAY_MS);
        if (not err) {

                if (sys_llist_push_back(uds->socket.list, socket) != socket) {
                        err = ENOMEM;
                }

                sys_mutex_unlock(uds->socket.mutex);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Unregister socket.
 *
 * @param  uds          UDS instance
 * @param  socket       socket
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int socket_unregister(uds_t *uds, UDS_socket_t *socket)
{
        int err = sys_mutex_lock(uds->socket.mutex, _MAX_DELAY_MS);
        if (not err) {
                err = ENOENT;

                int pos = sys_llist_find_begin(uds->socket.list, socket);
                if (pos >= 0) {
                        if (sys_llist_take(uds->socket.list, pos) == socket) {
                                err = 0;
                        }
                }

                sys_mutex_unlock(uds->socket.mutex);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Create socket buffer.
 *
 * @param  uds          instance
 * @param  socket       socket
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int buffer_create(uds_t *uds, UDS_socket_t *socket)
{
        UNUSED_ARG1(uds);

        int err = 0;

        if (not socket->buf) {
                bool   stream    = (socket->protocol == NETM_PROTOCOL__STREAM);
                size_t items     = stream ? STREAM_BUF_SIZE : MAX_DATAGRAM_BUFFERS;
                size_t item_size = stream ? 1 : sizeof(datagram_t);

                err = sys_queue_create(items, item_size, &socket->buf);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Reset socket buffer.
 *
 * @param  uds          instance
 * @param  socket       socket
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int buffer_reset(uds_t *uds, UDS_socket_t *socket)
{
        UNUSED_ARG1(uds);

        if (not socket->buf) {
                return 0;
        }

        if (socket->protocol == NETM_PROTOCOL__STREAM) {
                sys_queue_reset(socket->buf);

        } else if (socket->protocol == NETM_PROTOCOL__DATAGRAM) {
                datagram_t datagram;
                while (sys_queue_receive(socket->buf, &datagram, 0) == 0) {

                        if (datagram.from) {
                                zfree(cast(void **, &datagram.from));
                        }

                        zfree(cast(void **, &datagram.data));
                }
        }

        return 0;
}

//==============================================================================
/**
 * @brief  Receive data from datagram buffer.
 *
 * @param  uds          instance
 * @param  socket       socket
 * @param  buf          buffer for data
 * @param  len          number of bytes to receive
 * @param  addr         socket address
 * @param  recved       number of received bytes
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int buffer_datagram_receive(uds_t *uds, UDS_socket_t *socket, void *buf,
        size_t len, NETM_UDS_sockaddr_t *addr, size_t *recved)
{
        UNUSED_ARG1(uds);

        *recved = 0;

        if (socket->protocol != NETM_PROTOCOL__DATAGRAM) {
                return EINVAL;
        }

        datagram_t datagram;

        int err = sys_queue_receive(socket->buf, &datagram, socket->recv_timeout);
        if (err) {
                if (err == ETIME) {
                        err = 0;
                }

        } else {
                if (datagram.from) {
                        if (addr) {
                                sys_strlcpy(addr->path, datagram.from,
                                        sizeof(addr->path));
                        }

                        zfree(cast(void **, &datagram.from));
                }

                size_t n = min(datagram.len, len);

                memcpy(buf, datagram.data, n);

                zfree(cast(void **, &datagram.data));

                *recved = n;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Send datagram.
 *
 * @param  uds          instance
 * @param  socket       socket
 * @param  buffer       destination socket buffer
 * @param  data         data to send
 * @param  len          data length
 * @param  sent         number of sent bytes
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int buffer_datagram_send(uds_t *uds, UDS_socket_t *socket, kqueue_t *buffer,
        const void *data, size_t len, size_t *sent)
{
        UNUSED_ARG1(uds);

        *sent = 0;

        if (socket->protocol != NETM_PROTOCOL__DATAGRAM) {
                return EINVAL;
        }

        datagram_t datagram;

        datagram.len  = min(len, MAX_DATAGRAM_SIZE);
        datagram.data = NULL;
        datagram.from = NULL;

        int err = kalloc(datagram.len, cast(void **, &datagram.data));
        if (not err) {
                memcpy(datagram.data, data, datagram.len);
        }

        if (not err and socket->path) {
                size_t from_len = strsize(socket->path);
                err             = kalloc(from_len, cast(void **, &datagram.from));
                if (not err) {
                        sys_strlcpy(datagram.from, socket->path, from_len);
                }
        }

        if (not err) {
                err = sys_queue_send(buffer, &datagram, socket->send_timeout);
        }

        if (err) {
                if (datagram.data) {
                        zfree(cast(void **, &datagram.data));
                }

                if (datagram.from) {
                        zfree(cast(void **, &datagram.from));
                }
        } else {
                *sent = datagram.len;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Receive data from stream buffer.
 *
 * @param  uds          instance
 * @param  socket       socket
 * @param  buf          buffer for data
 * @param  len          number of bytes to receive
 * @param  recved       number of received bytes
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int buffer_stream_receive(uds_t *uds, UDS_socket_t *socket, void *buf, size_t len,
        size_t *recved)
{
        UNUSED_ARG1(uds);

        *recved = 0;

        if (socket->protocol != NETM_PROTOCOL__STREAM) {
                return EINVAL;
        }

        size_t n   = 0;
        int    err = EINVAL;

        while (len > 0) {
                err = sys_queue_receive(socket->buf, buf, socket->recv_timeout);
                if (err) {
                        if (err == ETIME) {
                                err = 0;
                        }
                        break;
                } else {
                        len--;
                        buf++;
                        n++;

                        size_t available;
                        if (sys_queue_get_number_of_items(socket->buf, &available) == 0) {
                                if (available == 0) {
                                        break;
                                }
                        }
                }
        }

        if (not err) {
                *recved = n;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Send stream.
 *
 * @param  uds          instance
 * @param  socket       socket
 * @param  buffer       destination socket buffer
 * @param  data         data to send
 * @param  len          data length
 * @param  sent         number of sent bytes
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int buffer_stream_send(uds_t *uds, UDS_socket_t *socket, kqueue_t *buffer,
        const void *buf, size_t len, size_t *sent)
{
        UNUSED_ARG1(uds);

        *sent = 0;

        if (socket->protocol != NETM_PROTOCOL__STREAM) {
                return EINVAL;
        }

        int    err = EINVAL;
        size_t n   = 0;

        while (len > 0) {
                err = sys_queue_send(buffer, buf, socket->send_timeout);
                if (err) {
                        break;

                } else {
                        buf++;
                        len--;
                        n++;
                }
        }

        if (not err) {
                *sent = n;
        }

        return err;
}

/*==============================================================================
  End of file
==============================================================================*/
