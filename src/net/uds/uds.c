/*==============================================================================
File     uds.c

Author   Daniel Zorychta

Brief    Unix Domain Socket Network management.

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
#define zalloc(_size, _pptr) _kzalloc(_MM_NET, _size, NULL, _MM_FLAG__DMA, _MM_FLAG__DMA, _pptr)

#define kalloc(_size, _pptr) _kmalloc(_MM_NET, _size, NULL, _MM_FLAG__DMA, _MM_FLAG__DMA, _pptr)

#define zfree(_pptr) _kfree(_MM_NET, _pptr)

#if __NETWORK_UDS_DEBUG_ON__ > 0
#define DEBUG(...) printk("UDS: "__VA_ARGS__)
#else
#define DEBUG(...)
#endif

#define STREAM_BUF_SIZE      __NETWORK_UDS_BUF_SIZE__
#define MAX_DATAGRAM_BUFFERS __NETWORK_UDS_DATAGRAM_BUFFERS_SIZE__
#define MAX_DATAGRAM_SIZE    __NETWORK_UDS_DATAGRAM_MAX_SIZE__
#define CONN_REQ_QUEUE_LEN   8

/*
 * Blocking operations wait in slices. After each slice the state of sockets
 * is checked, so destroy, disconnect, shutdown and interface down interrupt
 * waiting threads. Slice does not delay data transfer.
 */
#define WAIT_SLICE_MS 50

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        size_t len;
        char *from;
        u8_t *data;
} datagram_t;

typedef struct {
        struct {
                kmtx_t *mutex;
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

/**
 * Stream buffer (ring buffer). Data is copied outside critical section, only
 * indexes and wait flags are accessed in critical section. Buffer has single
 * writer (peer socket) and single reader (owner socket), because operations
 * of single socket are serialized by network manager.
 */
typedef struct UDS_stream {
        ksem_t *rx_sem; //!< signaled when data is available for waiting reader
        ksem_t *tx_sem; //!< signaled when space is available for waiting writer
        size_t size;    //!< buffer size
        size_t rd;      //!< read index
        size_t cnt;     //!< number of bytes in buffer
        bool rx_wait;   //!< reader waits for data
        bool tx_wait;   //!< writer waits for space
        u8_t data[];
} stream_t;

/** Operation deadline. */
typedef struct {
        clock_t tref;
        u32_t timeout;
} deadline_t;

/** Conditions that abort waiting operation. */
typedef struct {
        UDS_socket_t *self; //!< socket that performs operation (aborts when closing)
        UDS_socket_t *peer; //!< destination socket (aborts when closing), can be NULL
        bool link;          //!< aborts when self is not connected to peer anymore
        bool eof;           //!< aborts when self->eof is set
} wait_cond_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int stack_lock(uds_t *uds);
static void stack_unlock(uds_t *uds);
static bool path_is_valid(const char *path, size_t size);
static bool socket_is_registered(uds_t *uds, UDS_socket_t *socket);
static int socket_ref_get(uds_t *uds, UDS_socket_t *socket);
static void socket_ref_put(uds_t *uds, UDS_socket_t *socket);
static void socket_peer_detach(UDS_socket_t *socket);
static void socket_conn_req_finish(uds_t *uds, UDS_socket_t *req, int err);
static void deadline_start(deadline_t *dl, u32_t timeout);
static u32_t deadline_slice(const deadline_t *dl);
static int wait_check(uds_t *uds, const wait_cond_t *cond);
static int queue_wait(uds_t *uds, kqueue_t *queue, void *item, bool send, const deadline_t *dl,
        const wait_cond_t *cond);
static void operation_finish(uds_t *uds, UDS_socket_t *socket, UDS_socket_t *dst, bool rx, size_t bytes);
static int socket_register(uds_t *uds, UDS_socket_t *socket);
static int socket_unregister(uds_t *uds, UDS_socket_t *socket);
static int buffer_create(uds_t *uds, UDS_socket_t *socket);
static int buffer_reset(uds_t *uds, UDS_socket_t *socket);
static int buffer_datagram_receive(uds_t *uds, UDS_socket_t *socket, void *buf, size_t len, NETM_UDS_sockaddr_t *addr,
        size_t *recved);
static int buffer_datagram_send(uds_t *uds, UDS_socket_t *socket, UDS_socket_t *dst, const void *data, size_t len,
        size_t *sent);
static int stream_create(UDS_socket_t *socket);
static void stream_destroy(UDS_socket_t *socket);
static void stream_reset(UDS_socket_t *socket);
static void stream_wake(UDS_socket_t *socket);
static int stream_wait(uds_t *uds, ksem_t *sem, const deadline_t *dl, const wait_cond_t *cond);
static int buffer_stream_receive(uds_t *uds, UDS_socket_t *socket, void *buf, size_t len, size_t *recved);
static int buffer_stream_send(uds_t *uds, UDS_socket_t *socket, UDS_socket_t *dst, const void *buf, size_t len,
        size_t *sent);

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

        if (not err) {
                err = _llist_create_krn(_MM_NET, sys_llist_functor_cmp_pointers, NULL, &uds->socket.list);
        }

        if (err) {
                UDS_ifdeinit(uds);
                *ctx = NULL;
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

        if (not uds) {
                return EINVAL;
        }

        if (uds->socket.list and (sys_llist_size(uds->socket.list) > 0)) {
                DEBUG("stack in use\n");
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

        int err = stack_lock(uds);
        if (not err) {
                sys_critical_section_begin();
                uds->state = NETM_UDS_STATE__UP;
                sys_critical_section_end();
                stack_unlock(uds);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function turn down interface with static configuration.
 *         Blocked operations are interrupted with ENONET error.
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_ifdown(void *ctx)
{
        uds_t *uds = ctx;

        int err = stack_lock(uds);
        if (not err) {
                sys_critical_section_begin();
                uds->state = NETM_UDS_STATE__DOWN;
                sys_critical_section_end();
                stack_unlock(uds);
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
int UDS_ifstatus(void *ctx, NETM_UDS_status_t *status, size_t status_size)
{
        uds_t *uds = ctx;

        if (status_size < sizeof(*status)) {
                return EINVAL;
        }

        if (not uds) {
                return ENONET;
        }

        sys_critical_section_begin();
        {
                status->rx_packets = uds->stats.rx_packets;
                status->rx_bytes = uds->stats.rx_bytes;
                status->tx_packets = uds->stats.tx_packets;
                status->tx_bytes = uds->stats.tx_bytes;
                status->state = uds->state;
        }
        sys_critical_section_end();

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function create socket container.
 * @param  type         socket type
 * @param  prot         protocol
 * @param  socket       uds socket (object is already created)
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_create(void *ctx, NETM_socket_type_t type, u8_t prot, UDS_socket_t *socket)
{
        UNUSED_ARG1(prot);

        uds_t *uds = ctx;

        if ((type != NETM_SOCKET_TYPE__STREAM) and (type != NETM_SOCKET_TYPE__DATAGRAM)) {
                return ENOTSUP;
        }

        socket->path = NULL;
        socket->buf = NULL;
        socket->stream = NULL;
        socket->conn_req = NULL;
        socket->conn_sem = NULL;
        socket->conn_sock = NULL;
        socket->type = type;
        socket->recv_timeout = _WAIT_FOREVER_MS;
        socket->send_timeout = _WAIT_FOREVER_MS;
        socket->shutdown = 0;
        socket->conn_err = 0;
        socket->refs = 0;
        socket->connected = false;
        socket->connecting = false;
        socket->closing = false;
        socket->eof = false;

        return socket_register(uds, socket);
}

//==============================================================================
/**
 * @brief  Function destroy created socket.
 *
 * Connected peer is detached, clients waiting in accept queue are refused and
 * all threads blocked on the socket are woken up. Function waits until all
 * pending operations that use the socket are finished.
 *
 * @param  socket    uds socket to destroy
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_destroy(void *ctx, UDS_socket_t *socket)
{
        uds_t *uds = ctx;

        int err = stack_lock(uds);
        if (err) {
                return err;
        }

        err = socket_unregister(uds, socket);
        if (not err) {
                UDS_socket_t *peer = NULL;

                sys_critical_section_begin();
                {
                        socket->closing = true;
                        socket->connecting = false;

                        if (socket->conn_sock and (socket->conn_sock->conn_sock == socket)) {
                                peer = socket->conn_sock;
                                peer->eof = true;
                        }

                        socket_peer_detach(socket);
                }
                sys_critical_section_end();

                // wake up threads waiting for data or space
                stream_wake(socket);
                if (peer) {
                        stream_wake(peer);
                }

                if (socket->conn_req) {
                        UDS_socket_t *req;
                        while (sys_queue_receive(socket->conn_req, &req, 0) == ESUCC) {
                                socket_conn_req_finish(uds, req, ECONNREFUSED);
                        }
                }
        }

        stack_unlock(uds);

        if (err) {
                return err;
        }

        // wait for operations in progress (blocked operations exit within one wait slice)
        for (;;) {
                sys_critical_section_begin();
                u16_t refs = socket->refs;
                sys_critical_section_end();

                if (refs == 0) {
                        break;
                }

                sys_sleep_ms(1);
        }

        // socket is not registered, so it cannot be found by path anymore
        if (socket->path) {
                zfree(cast(void **, &socket->path));
        }

        if (socket->buf) {
                buffer_reset(uds, socket);
                sys_queue_destroy(socket->buf);
                socket->buf = NULL;
        }

        stream_destroy(socket);

        if (socket->conn_req) {
                sys_queue_destroy(socket->conn_req);
                socket->conn_req = NULL;
        }

        if (socket->conn_sem) {
                sys_semaphore_destroy(socket->conn_sem);
                socket->conn_sem = NULL;
        }

        return ESUCC;
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
int UDS_socket_connect(void *ctx, UDS_socket_t *socket, const NETM_UDS_sockaddr_t *addr, size_t addr_size)
{
        uds_t *uds = ctx;

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        if ((addr_size != sizeof(*addr)) or not path_is_valid(addr->path, sizeof(addr->path))) {
                return EINVAL;
        }

        int err = socket_ref_get(uds, socket);
        if (err) {
                return err;
        }

        if ((err = stack_lock(uds)) != ESUCC) {
                socket_ref_put(uds, socket);
                return err;
        }

        if (socket->connected or socket->conn_sock or socket->connecting) {
                err = EISCONN;
        }

        if (not err) {
                err = buffer_create(uds, socket);
        }

        if (not err and not socket->conn_sem) {
                err = sys_semaphore_create(1, 0, &socket->conn_sem);
        }

        if (not err) {
                // drop data of previous connection
                buffer_reset(uds, socket);

                err = ENOENT;

                sys_llist_foreach (UDS_socket_t *, s, uds->socket.list) {
                        if (s->path and s->conn_req and isstreq(s->path, addr->path)) {

                                if (s->type != socket->type) {
                                        err = ECONNREFUSED;

                                } else {
                                        socket->conn_err = 0;
                                        socket->connecting = true;

                                        sys_critical_section_begin();
                                        socket->eof = false;
                                        sys_critical_section_end();

                                        // never block here, the stack mutex is locked
                                        err = sys_queue_send(s->conn_req, &socket, 0);
                                        if (err) {
                                                socket->connecting = false;
                                                err = ECONNREFUSED;
                                        }
                                }

                                break;
                        }
                }
        }

        stack_unlock(uds);

        if (not err) {
                deadline_t dl;
                deadline_start(&dl, socket->recv_timeout);

                for (;;) {
                        u32_t slice = deadline_slice(&dl);

                        int werr = ETIME;
                        if (slice > 0) {
                                werr = sys_semaphore_wait(socket->conn_sem, slice);
                        }

                        stack_lock(uds);

                        bool done = true;

                        if (socket->conn_sock) {
                                // accepted (connected flag is set by server)
                                socket->connecting = false;
                                err = ESUCC;

                        } else if (not socket->connecting) {
                                // refused by server or server destroyed
                                err = socket->conn_err ? socket->conn_err : ECONNREFUSED;

                        } else if (socket->closing) {
                                err = ECONNRESET;

                        } else if (uds->state == NETM_UDS_STATE__DOWN) {
                                err = ENONET;

                        } else if (werr == ECANCELED) {
                                err = ECANCELED;

                        } else if (slice == 0) {
                                err = ETIME;

                        } else {
                                done = false;
                        }

                        if (done and err) {
                                // accept() ignores request that is not connecting anymore
                                socket->connecting = false;
                        }

                        stack_unlock(uds);

                        if (done) {
                                break;
                        }
                }
        }

        socket_ref_put(uds, socket);

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

        int err = stack_lock(uds);
        if (not err) {
                if (socket->conn_sock) {
                        UDS_socket_t *peer = socket->conn_sock;

                        sys_critical_section_begin();
                        {
                                if (peer->conn_sock == socket) {
                                        peer->eof = true;
                                }

                                socket_peer_detach(socket);
                        }
                        sys_critical_section_end();

                        stream_wake(socket);
                        stream_wake(peer);
                } else {
                        err = ENOENT;
                }

                stack_unlock(uds);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function shutdown selected socket direction.
 *         Write shutdown of stream socket signals end of data to the peer.
 * @param  socket       socket
 * @param  how          shutdown direction
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_shutdown(void *ctx, UDS_socket_t *socket, NETM_shut_t how)
{
        uds_t *uds = ctx;

        if (how & ~NETM_SHUT__RDWR) {
                return EINVAL;
        }

        int err = stack_lock(uds);
        if (not err) {
                socket->shutdown = cast(NETM_shut_t, socket->shutdown | how);

                if ((how & NETM_SHUT__WR) and (socket->type == NETM_SOCKET_TYPE__STREAM) and socket->conn_sock) {
                        sys_critical_section_begin();
                        socket->conn_sock->eof = true;
                        sys_critical_section_end();

                        stream_wake(socket->conn_sock);
                }

                stack_unlock(uds);
        }

        return err;
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
int UDS_socket_bind(void *ctx, UDS_socket_t *socket, const NETM_UDS_sockaddr_t *addr, size_t addr_size)
{
        uds_t *uds = ctx;

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        if ((addr_size != sizeof(*addr)) or not path_is_valid(addr->path, sizeof(addr->path))) {
                return EINVAL;
        }

        size_t path_len = strnlen(addr->path, sizeof(addr->path));

        int err = stack_lock(uds);
        if (err) {
                return err;
        }

        sys_llist_foreach (UDS_socket_t *, s, uds->socket.list) {
                if ((s != socket) and s->path and isstreq(s->path, addr->path)) {
                        err = EADDRINUSE;
                        break;
                }
        }

        if (not err and (socket->type == NETM_SOCKET_TYPE__DATAGRAM)) {
                err = buffer_create(uds, socket);
        }

        if (not err) {
                char *new_path = NULL;

                err = kalloc(path_len + 1, cast(void **, &new_path));
                if (not err) {
                        sys_strlcpy(new_path, addr->path, path_len + 1);

                        if (socket->path) {
                                zfree(cast(void **, &socket->path));
                        }

                        socket->path = new_path;
                }
        }

        stack_unlock(uds);

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

        int err = stack_lock(uds);
        if (not err) {
                if (not socket->path) {
                        err = EINVAL;

                } else if (not socket->conn_req) {
                        err = sys_queue_create(CONN_REQ_QUEUE_LEN, sizeof(UDS_socket_t *), &socket->conn_req);
                }

                stack_unlock(uds);
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

        if (not socket->conn_req) {
                return EINVAL;
        }

        int err = socket_ref_get(uds, socket);
        if (err) {
                return err;
        }

        deadline_t dl;
        deadline_start(&dl, socket->recv_timeout);

        const wait_cond_t cond = {.self = socket};

        for (bool done = false; not done;) {

                UDS_socket_t *req = NULL;

                err = queue_wait(uds, socket->conn_req, &req, false, &dl, &cond);
                if (err) {
                        break;
                }

                if ((err = stack_lock(uds)) != ESUCC) {
                        break;
                }

                done = true;

                if (socket->closing) {
                        socket_conn_req_finish(uds, req, ECONNREFUSED);
                        err = ECONNABORTED;

                } else if (not req or not socket_is_registered(uds, req) or not req->connecting) {
                        // client gave up (timeout) or was destroyed, wait for next request
                        done = false;

                } else if (req->type != socket->type) {
                        socket_conn_req_finish(uds, req, ECONNREFUSED);
                        done = false;

                } else {
                        err = UDS_socket_create(uds, socket->type, 0, new_socket);
                        if (not err) {
                                err = buffer_create(uds, new_socket);
                                if (err) {
                                        UDS_socket_destroy(uds, new_socket);
                                }
                        }

                        if (err) {
                                socket_conn_req_finish(uds, req, err);

                        } else {
                                sys_critical_section_begin();
                                {
                                        new_socket->conn_sock = req;
                                        new_socket->connected = true;
                                        req->conn_sock = new_socket;
                                }
                                sys_critical_section_end();

                                socket_conn_req_finish(uds, req, ESUCC);
                        }
                }

                stack_unlock(uds);
        }

        socket_ref_put(uds, socket);

        return err;
}

//==============================================================================
/**
 * @brief  Function receive incoming data.
 *
 * Stream socket: when peer closed connection (or shut down write direction)
 * remaining data can be still received, after that ECONNRESET is returned.
 * Timeout without data is not an error (0 bytes received).
 *
 * @param  socket       socket
 * @param  buf          buffer for data
 * @param  len          number of data to receive
 * @param  flags        flags
 * @param  recved       number of received bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int UDS_socket_recv(void *ctx, UDS_socket_t *socket, void *buf, size_t len, NETM_flags_t flags, size_t *recved)
{
        uds_t *uds = ctx;

        *recved = 0;

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        if (socket->shutdown & NETM_SHUT__RD) {
                return EPERM;
        }

        int err = ESUCC;

        sys_critical_section_begin();

        if (socket->closing) {
                err = EBADF;

        } else if (socket->type == NETM_SOCKET_TYPE__STREAM) {
                // remaining data can be received after peer closed connection
                if (not socket->stream or not((socket->conn_sock and socket->connected) or socket->eof)) {
                        err = ECONNREFUSED;
                }

        } else if (socket->type == NETM_SOCKET_TYPE__DATAGRAM) {
                // datagram socket can receive when bound or connected
                if (not socket->buf) {
                        err = ECONNREFUSED;
                }

        } else {
                err = EINVAL;
        }

        if (not err) {
                socket->refs++;
        }

        sys_critical_section_end();

        if (err) {
                return err;
        }

        if (socket->type == NETM_SOCKET_TYPE__STREAM) {
                err = buffer_stream_receive(uds, socket, buf, len, recved);

                if (flags & NETM_FLAGS__FREEBUF) {
                        buffer_reset(uds, socket);
                }

        } else {
                // NETM_FLAGS__FREEBUF: rest of datagram is always dropped
                err = buffer_datagram_receive(uds, socket, buf, len, NULL, recved);
        }

        operation_finish(uds, socket, NULL, true, err ? 0 : *recved);

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
int UDS_socket_recvfrom(void *ctx, UDS_socket_t *socket, void *buf, size_t len, NETM_flags_t flags,
        NETM_UDS_sockaddr_t *addr, size_t addr_size, size_t *recved)
{
        UNUSED_ARG1(flags);

        uds_t *uds = ctx;

        *recved = 0;

        if (addr_size != sizeof(*addr)) {
                return EINVAL;
        }

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        if (socket->type != NETM_SOCKET_TYPE__DATAGRAM) {
                return ECANCELED;
        }

        if (socket->shutdown & NETM_SHUT__RD) {
                return EPERM;
        }

        if (not socket->buf) {
                return ECONNREFUSED;
        }

        int err = socket_ref_get(uds, socket);
        if (err) {
                return err;
        }

        // NETM_FLAGS__FREEBUF: rest of datagram is always dropped
        err = buffer_datagram_receive(uds, socket, buf, len, addr, recved);

        operation_finish(uds, socket, NULL, true, err ? 0 : *recved);

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
int UDS_socket_send(void *ctx, UDS_socket_t *socket, const void *buf, size_t len, NETM_flags_t flags, size_t *sent)
{
        UNUSED_ARG1(flags);

        uds_t *uds = ctx;

        *sent = 0;

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        if (socket->shutdown & NETM_SHUT__WR) {
                return EPIPE;
        }

        UDS_socket_t *dst = NULL;
        int err = ESUCC;

        sys_critical_section_begin();

        if (socket->closing) {
                err = EBADF;

        } else if (socket->conn_sock and socket->connected and (socket->conn_sock->buf or socket->conn_sock->stream)) {
                dst = socket->conn_sock;
                dst->refs++;
                socket->refs++;

        } else {
                err = socket->eof ? EPIPE : ECONNREFUSED;
        }

        sys_critical_section_end();

        if (err) {
                return err;
        }

        if (socket->type == NETM_SOCKET_TYPE__STREAM) {
                err = buffer_stream_send(uds, socket, dst, buf, len, sent);

        } else if (socket->type == NETM_SOCKET_TYPE__DATAGRAM) {
                err = buffer_datagram_send(uds, socket, dst, buf, len, sent);

        } else {
                err = EINVAL;
        }

        operation_finish(uds, socket, dst, false, err ? 0 : *sent);

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
int UDS_socket_sendto(void *ctx, UDS_socket_t *socket, const void *buf, size_t len, NETM_flags_t flags,
        const NETM_UDS_sockaddr_t *to_addr, size_t to_addr_size, size_t *sent)
{
        UNUSED_ARG1(flags);

        uds_t *uds = ctx;

        *sent = 0;

        if (uds->state == NETM_UDS_STATE__DOWN) {
                return ENONET;
        }

        if ((to_addr_size != sizeof(*to_addr)) or not path_is_valid(to_addr->path, sizeof(to_addr->path))) {
                return EINVAL;
        }

        if (socket->type != NETM_SOCKET_TYPE__DATAGRAM) {
                return ECANCELED;
        }

        if (socket->shutdown & NETM_SHUT__WR) {
                return EPIPE;
        }

        UDS_socket_t *dst = NULL;

        int err = stack_lock(uds);
        if (err) {
                return err;
        }

        if (socket->closing) {
                err = EBADF;

        } else {
                err = ENOENT;

                sys_llist_foreach (UDS_socket_t *, s, uds->socket.list) {
                        if (s->path and isstreq(s->path, to_addr->path)) {
                                if (s->type != socket->type) {
                                        err = ECONNABORTED;
                                } else if (not s->buf) {
                                        err = ECONNREFUSED;
                                } else {
                                        dst = s;

                                        sys_critical_section_begin();
                                        if (socket->closing) {
                                                err = EBADF;
                                        } else {
                                                dst->refs++;
                                                socket->refs++;
                                                err = ESUCC;
                                        }
                                        sys_critical_section_end();
                                }
                                break;
                        }
                }
        }

        stack_unlock(uds);

        if (err) {
                return err;
        }

        err = buffer_datagram_send(uds, socket, dst, buf, len, sent);

        operation_finish(uds, socket, dst, false, err ? 0 : *sent);

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
int UDS_gethostbyname(void *ctx, const char *name, NETM_UDS_sockaddr_t *addr, size_t addr_size)
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
int UDS_socket_getaddress(void *ctx, UDS_socket_t *socket, NETM_UDS_sockaddr_t *addr, size_t addr_size)
{
        uds_t *uds = ctx;

        if (addr_size != sizeof(*addr)) {
                return EINVAL;
        }

        int err = stack_lock(uds);
        if (not err) {
                const char *path = socket->path;

                if (socket->conn_sock and socket->connected) {
                        path = socket->conn_sock->path;
                }

                // accepted and not bound sockets have no path
                sys_strlcpy(addr->path, path ? path : "", sizeof(addr->path));

                stack_unlock(uds);
        }

        return err;
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
 * @brief  Lock stack mutex.
 *
 * Lock is retried when thread kill is requested, because bookkeeping
 * (e.g. socket references) must be always finished.
 *
 * @param  uds          UDS instance
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static int stack_lock(uds_t *uds)
{
        int err;

        do {
                err = sys_mutex_lock(uds->socket.mutex, _WAIT_FOREVER_MS);
        } while ((err == ECANCELED) or (err == ETIME));

        return err;
}

//==============================================================================
/**
 * @brief  Unlock stack mutex.
 *
 * @param  uds          UDS instance
 */
//==============================================================================
static void stack_unlock(uds_t *uds)
{
        sys_mutex_unlock(uds->socket.mutex);
}

//==============================================================================
/**
 * @brief  Check if socket path is NUL terminated within buffer and not empty.
 *
 * @param  path         path buffer
 * @param  size         path buffer size
 *
 * @return True if path is valid.
 */
//==============================================================================
static bool path_is_valid(const char *path, size_t size)
{
        size_t len = strnlen(path, size);
        return (len > 0) and (len < size);
}

//==============================================================================
/**
 * @brief  Check if socket is registered in stack. Socket object is not
 *         dereferenced, so the function is safe for already destroyed sockets.
 *         Stack mutex must be locked.
 *
 * @param  uds          UDS instance
 * @param  socket       socket
 *
 * @return True if socket is registered.
 */
//==============================================================================
static bool socket_is_registered(uds_t *uds, UDS_socket_t *socket)
{
        return sys_llist_find_begin(uds->socket.list, socket) >= 0;
}

//==============================================================================
/**
 * @brief  Mark socket as used by pending operation. Socket is not released by
 *         destroy function until reference is put back. Function can be called
 *         without stack mutex (critical section is used).
 *
 * @param  uds          UDS instance
 * @param  socket       socket
 *
 * @return One of @ref errno value (EBADF when socket is being destroyed).
 */
//==============================================================================
static int socket_ref_get(uds_t *uds, UDS_socket_t *socket)
{
        UNUSED_ARG1(uds);

        int err = ESUCC;

        sys_critical_section_begin();

        if (socket->closing) {
                err = EBADF;
        } else {
                socket->refs++;
        }

        sys_critical_section_end();

        return err;
}

//==============================================================================
/**
 * @brief  Release socket reference.
 *
 * @param  uds          UDS instance
 * @param  socket       socket
 */
//==============================================================================
static void socket_ref_put(uds_t *uds, UDS_socket_t *socket)
{
        UNUSED_ARG1(uds);

        sys_critical_section_begin();

        if (socket->refs > 0) {
                socket->refs--;
        }

        sys_critical_section_end();
}

//==============================================================================
/**
 * @brief  Unlink socket and its peer. Stack mutex must be locked and function
 *         must be called in critical section.
 *
 * @param  socket       socket
 */
//==============================================================================
static void socket_peer_detach(UDS_socket_t *socket)
{
        UDS_socket_t *peer = socket->conn_sock;

        if (peer and (peer->conn_sock == socket)) {
                peer->conn_sock = NULL;
                peer->connected = false;
        }

        socket->conn_sock = NULL;
        socket->connected = false;
}

//==============================================================================
/**
 * @brief  Finish client connection request (accepted or rejected) and wake up
 *         client. Stack mutex must be locked. Request of client that gave up
 *         or does not exist anymore is ignored.
 *
 * @param  uds          UDS instance
 * @param  req          requesting (client) socket
 * @param  err          ESUCC: accepted (conn_sock is already set), otherwise error
 */
//==============================================================================
static void socket_conn_req_finish(uds_t *uds, UDS_socket_t *req, int err)
{
        if (req and socket_is_registered(uds, req) and req->connecting) {
                req->conn_err = err;
                req->connecting = false;

                sys_critical_section_begin();
                req->connected = (err == ESUCC);
                sys_critical_section_end();

                if (req->conn_sem) {
                        sys_semaphore_signal(req->conn_sem);
                }
        }
}

//==============================================================================
/**
 * @brief  Start operation deadline.
 *
 * @param  dl           deadline
 * @param  timeout      timeout in milliseconds
 */
//==============================================================================
static void deadline_start(deadline_t *dl, u32_t timeout)
{
        dl->timeout = timeout;
        dl->tref = (timeout < _WAIT_FOREVER_MS) ? sys_get_uptime_ms() : 0;
}

//==============================================================================
/**
 * @brief  Return next wait slice.
 *
 * @param  dl           deadline
 *
 * @return Wait time in milliseconds, 0 when deadline expired.
 */
//==============================================================================
static u32_t deadline_slice(const deadline_t *dl)
{
        if (dl->timeout >= _WAIT_FOREVER_MS) {
                return WAIT_SLICE_MS;
        }

        clock_t elapsed = sys_get_uptime_ms() - dl->tref;

        if (elapsed >= cast(clock_t, dl->timeout)) {
                return 0;
        }

        clock_t left = cast(clock_t, dl->timeout) - elapsed;

        return (left < WAIT_SLICE_MS) ? cast(u32_t, left) : WAIT_SLICE_MS;
}

//==============================================================================
/**
 * @brief  Check if waiting operation should be aborted.
 *
 * @param  uds          UDS instance
 * @param  cond         abort conditions
 *
 * @return ESUCC when operation can wait further, otherwise error.
 */
//==============================================================================
static int wait_check(uds_t *uds, const wait_cond_t *cond)
{
        int err = ESUCC;

        sys_critical_section_begin();
        {
                if (uds->state == NETM_UDS_STATE__DOWN) {
                        err = ENONET;

                } else if (cond->self and cond->self->closing) {
                        err = ECONNRESET;

                } else if (cond->peer and cond->peer->closing) {
                        err = ECONNRESET;

                } else if (cond->link and (cond->self->conn_sock != cond->peer)) {
                        err = ECONNRESET;

                } else if (cond->eof and cond->self->eof) {
                        err = ECONNRESET;
                }
        }
        sys_critical_section_end();

        return err;
}

//==============================================================================
/**
 * @brief  Send item to queue or receive item from queue with deadline.
 *
 * Send: first attempt does not block (queue is usually not full).
 * Receive: abort conditions are checked first and then function waits
 * (failed non-blocking attempt is avoided when receiver waits for data).
 * Waiting is divided into slices and abort conditions are checked between
 * slices.
 *
 * @param  uds          UDS instance
 * @param  queue        queue
 * @param  item         item
 * @param  send         true: send, false: receive
 * @param  dl           deadline
 * @param  cond         abort conditions
 *
 * @return One of @ref errno value (ETIME when deadline expired).
 */
//==============================================================================
static int queue_wait(uds_t *uds, kqueue_t *queue, void *item, bool send, const deadline_t *dl, const wait_cond_t *cond)
{
        u32_t slice = 0;

        if (not send) {
                int err = wait_check(uds, cond);
                if (err) {
                        // data could be sent just before abort condition was set
                        return (sys_queue_receive(queue, item, 0) == ESUCC) ? ESUCC : err;
                }

                slice = deadline_slice(dl);
        }

        for (;;) {
                int err = send ? sys_queue_send(queue, item, slice) : sys_queue_receive(queue, item, slice);
                if (err != ETIME) {
                        return err;
                }

                err = wait_check(uds, cond);
                if (err) {
                        // peer could send last data just before it closed connection
                        if (not send and (sys_queue_receive(queue, item, 0) == ESUCC)) {
                                return ESUCC;
                        }

                        return err;
                }

                slice = deadline_slice(dl);
                if (slice == 0) {
                        return ETIME;
                }
        }
}

//==============================================================================
/**
 * @brief  Finish socket operation: release references of socket and
 *         destination socket and update interface statistics.
 *
 * @param  uds          UDS instance
 * @param  socket       socket that performed operation (referenced)
 * @param  dst          destination socket (referenced), can be NULL
 * @param  rx           true: RX direction, false: TX direction
 * @param  bytes        number of transferred bytes
 */
//==============================================================================
static void operation_finish(uds_t *uds, UDS_socket_t *socket, UDS_socket_t *dst, bool rx, size_t bytes)
{
        sys_critical_section_begin();
        {
                if (socket->refs > 0) {
                        socket->refs--;
                }

                if (dst and (dst->refs > 0)) {
                        dst->refs--;
                }

                if (bytes > 0) {
                        if (rx) {
                                uds->stats.rx_bytes += bytes;
                                uds->stats.rx_packets++;
                        } else {
                                uds->stats.tx_bytes += bytes;
                                uds->stats.tx_packets++;
                        }
                }
        }
        sys_critical_section_end();
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
        int err = stack_lock(uds);
        if (not err) {

                if (sys_llist_push_back(uds->socket.list, socket) != socket) {
                        err = ENOMEM;
                }

                stack_unlock(uds);
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
        int err = stack_lock(uds);
        if (not err) {
                err = ENOENT;

                int pos = sys_llist_find_begin(uds->socket.list, socket);
                if (pos >= 0) {
                        if (sys_llist_take(uds->socket.list, pos) == socket) {
                                err = 0;
                        }
                }

                stack_unlock(uds);
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

        if (socket->type == NETM_SOCKET_TYPE__STREAM) {
                if (not socket->stream) {
                        err = stream_create(socket);
                }

        } else if (not socket->buf) {
                err = sys_queue_create(MAX_DATAGRAM_BUFFERS, sizeof(datagram_t), &socket->buf);
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

        if (socket->type == NETM_SOCKET_TYPE__STREAM) {
                stream_reset(socket);

        } else if (socket->buf) {
                datagram_t datagram;
                while (sys_queue_receive(socket->buf, &datagram, 0) == 0) {

                        if (datagram.from) {
                                zfree(cast(void **, &datagram.from));
                        }

                        if (datagram.data) {
                                zfree(cast(void **, &datagram.data));
                        }
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
static int buffer_datagram_receive(uds_t *uds, UDS_socket_t *socket, void *buf, size_t len, NETM_UDS_sockaddr_t *addr,
        size_t *recved)
{
        *recved = 0;

        if (socket->type != NETM_SOCKET_TYPE__DATAGRAM) {
                return EINVAL;
        }

        if (addr) {
                addr->path[0] = '\0';
        }

        deadline_t dl;
        deadline_start(&dl, socket->recv_timeout);

        const wait_cond_t cond = {.self = socket};

        datagram_t datagram;

        int err = queue_wait(uds, socket->buf, &datagram, false, &dl, &cond);
        if (err) {
                if (err == ETIME) {
                        err = 0;
                }

        } else {
                if (datagram.from) {
                        if (addr) {
                                sys_strlcpy(addr->path, datagram.from, sizeof(addr->path));
                        }

                        zfree(cast(void **, &datagram.from));
                }

                size_t n = min(datagram.len, len);

                if (n > 0) {
                        memcpy(buf, datagram.data, n);
                }

                if (datagram.data) {
                        zfree(cast(void **, &datagram.data));
                }

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
 * @param  dst          destination socket (referenced)
 * @param  data         data to send
 * @param  len          data length
 * @param  sent         number of sent bytes
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int buffer_datagram_send(uds_t *uds, UDS_socket_t *socket, UDS_socket_t *dst, const void *data, size_t len,
        size_t *sent)
{
        *sent = 0;

        if (socket->type != NETM_SOCKET_TYPE__DATAGRAM) {
                return EINVAL;
        }

        if (len > MAX_DATAGRAM_SIZE) {
                return EINVAL;
        }

        datagram_t datagram;

        datagram.len = len;
        datagram.data = NULL;
        datagram.from = NULL;

        // allocate at least 1 byte to support zero-length datagrams
        int err = kalloc(len ? len : 1, cast(void **, &datagram.data));
        if (not err and (len > 0)) {
                memcpy(datagram.data, data, len);
        }

        if (not err and socket->path) {
                size_t from_len = strsize(socket->path);
                err = kalloc(from_len, cast(void **, &datagram.from));
                if (not err) {
                        sys_strlcpy(datagram.from, socket->path, from_len);
                }
        }

        if (not err) {
                deadline_t dl;
                deadline_start(&dl, socket->send_timeout);

                const wait_cond_t cond = {.self = socket, .peer = dst};

                err = queue_wait(uds, dst->buf, &datagram, true, &dl, &cond);
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
 * @brief  Create stream buffer.
 *
 * @param  socket       socket
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static int stream_create(UDS_socket_t *socket)
{
        stream_t *stream = NULL;

        int err = zalloc(sizeof(stream_t) + STREAM_BUF_SIZE, cast(void **, &stream));
        if (err) {
                return err;
        }

        stream->size = STREAM_BUF_SIZE;

        err = sys_semaphore_create(1, 0, &stream->rx_sem);

        if (not err) {
                err = sys_semaphore_create(1, 0, &stream->tx_sem);
        }

        if (err) {
                if (stream->rx_sem) {
                        sys_semaphore_destroy(stream->rx_sem);
                }

                zfree(cast(void **, &stream));
                return err;
        }

        socket->stream = stream;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Destroy stream buffer.
 *
 * @param  socket       socket
 */
//==============================================================================
static void stream_destroy(UDS_socket_t *socket)
{
        stream_t *stream = socket->stream;

        if (stream) {
                socket->stream = NULL;
                sys_semaphore_destroy(stream->rx_sem);
                sys_semaphore_destroy(stream->tx_sem);
                zfree(cast(void **, &stream));
        }
}

//==============================================================================
/**
 * @brief  Drop all data of stream buffer.
 *
 * Read index is moved to the write position, so data that is just copied by
 * writer (not committed yet) stays consistent.
 *
 * @param  socket       socket
 */
//==============================================================================
static void stream_reset(UDS_socket_t *socket)
{
        stream_t *stream = socket->stream;

        if (stream) {
                sys_critical_section_begin();
                stream->rd = (stream->rd + stream->cnt) % stream->size;
                stream->cnt = 0;
                bool wake = stream->tx_wait;
                stream->tx_wait = false;
                sys_critical_section_end();

                if (wake) {
                        sys_semaphore_signal(stream->tx_sem);
                }
        }
}

//==============================================================================
/**
 * @brief  Wake up threads that wait for data or space of stream buffer
 *         (state of sockets was changed). Stack mutex must be locked.
 *
 * @param  socket       socket (registered or destroyed by caller)
 */
//==============================================================================
static void stream_wake(UDS_socket_t *socket)
{
        if (socket and socket->stream) {
                sys_semaphore_signal(socket->stream->rx_sem);
                sys_semaphore_signal(socket->stream->tx_sem);
        }
}

//==============================================================================
/**
 * @brief  Wait for stream buffer event (data or space).
 *
 * @param  uds          UDS instance
 * @param  sem          event semaphore
 * @param  dl           deadline
 * @param  cond         abort conditions
 *
 * @return ESUCC when caller should check the buffer again, ETIME when deadline
 *         expired, otherwise error.
 */
//==============================================================================
static int stream_wait(uds_t *uds, ksem_t *sem, const deadline_t *dl, const wait_cond_t *cond)
{
        int err = wait_check(uds, cond);
        if (err) {
                return err;
        }

        u32_t slice = deadline_slice(dl);
        if (slice == 0) {
                return ETIME;
        }

        err = sys_semaphore_wait(sem, slice);

        return (err == ETIME) ? ESUCC : err;
}

//==============================================================================
/**
 * @brief  Receive data from stream buffer.
 *
 * Function waits for data and then takes all available bytes (up to len).
 * When peer closed connection, remaining data is received first.
 *
 * @param  uds          instance
 * @param  socket       socket
 * @param  buf          buffer for data
 * @param  len          number of bytes to receive
 * @param  recved       number of received bytes
 *
 * @return On success 0 is returned (also on timeout without data).
 */
//==============================================================================
static int buffer_stream_receive(uds_t *uds, UDS_socket_t *socket, void *buf, size_t len, size_t *recved)
{
        *recved = 0;

        stream_t *stream = socket->stream;

        if ((socket->type != NETM_SOCKET_TYPE__STREAM) or not stream) {
                return EINVAL;
        }

        if (len == 0) {
                return 0;
        }

        deadline_t dl;
        deadline_start(&dl, socket->recv_timeout);

        const wait_cond_t cond = {.self = socket, .eof = true};

        for (;;) {
                sys_critical_section_begin();
                size_t rd = stream->rd;
                size_t cnt = stream->cnt;
                if (cnt == 0) {
                        stream->rx_wait = true;
                }
                sys_critical_section_end();

                if (cnt > 0) {
                        u8_t *dst = buf;
                        size_t n = min(cnt, len);
                        size_t part = min(n, stream->size - rd);

                        memcpy(dst, &stream->data[rd], part);
                        if (n > part) {
                                memcpy(&dst[part], &stream->data[0], n - part);
                        }

                        sys_critical_section_begin();
                        stream->rd = (rd + n) % stream->size;
                        stream->cnt -= n;
                        bool wake = stream->tx_wait;
                        stream->tx_wait = false;
                        sys_critical_section_end();

                        if (wake) {
                                sys_semaphore_signal(stream->tx_sem);
                        }

                        *recved = n;
                        return 0;
                }

                int err = stream_wait(uds, stream->rx_sem, &dl, &cond);
                if (err) {
                        // data could be sent just before abort condition was set
                        sys_critical_section_begin();
                        cnt = stream->cnt;
                        sys_critical_section_end();

                        if (cnt == 0) {
                                // timeout without data is not an error
                                return (err == ETIME) ? 0 : err;
                        }
                }
        }
}

//==============================================================================
/**
 * @brief  Send stream.
 *
 * Send timeout applies to the whole operation. When timeout expires or
 * connection is closed after part of data was sent, number of sent bytes
 * is returned without error. When connection is closed by peer before any
 * byte was sent, EPIPE is returned.
 *
 * @param  uds          instance
 * @param  socket       socket
 * @param  dst          destination socket (referenced)
 * @param  buf          data to send
 * @param  len          data length
 * @param  sent         number of sent bytes
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int buffer_stream_send(uds_t *uds, UDS_socket_t *socket, UDS_socket_t *dst, const void *buf, size_t len,
        size_t *sent)
{
        *sent = 0;

        stream_t *stream = dst->stream;

        if ((socket->type != NETM_SOCKET_TYPE__STREAM) or not stream) {
                return EINVAL;
        }

        const u8_t *src = buf;
        size_t n = 0;
        int err = 0;

        deadline_t dl;
        deadline_start(&dl, socket->send_timeout);

        const wait_cond_t cond = {.self = socket, .peer = dst, .link = true};

        while (n < len) {
                sys_critical_section_begin();
                size_t cnt = stream->cnt;
                size_t wr = (stream->rd + cnt) % stream->size;
                size_t space = stream->size - cnt;
                if (space == 0) {
                        stream->tx_wait = true;
                }
                sys_critical_section_end();

                if (space == 0) {
                        err = stream_wait(uds, stream->tx_sem, &dl, &cond);
                        if (err) {
                                if ((err == ECONNRESET) and not socket->closing) {
                                        err = EPIPE;
                                }
                                break;
                        }
                        continue;
                }

                size_t chunk = min(space, len - n);
                size_t part = min(chunk, stream->size - wr);

                memcpy(&stream->data[wr], &src[n], part);
                if (chunk > part) {
                        memcpy(&stream->data[0], &src[n + part], chunk - part);
                }

                sys_critical_section_begin();
                stream->cnt += chunk;
                bool wake = stream->rx_wait;
                stream->rx_wait = false;
                sys_critical_section_end();

                if (wake) {
                        sys_semaphore_signal(stream->rx_sem);
                }

                n += chunk;
        }

        // bytes already queued must be reported, otherwise caller duplicates them
        *sent = n;

        return (n > 0) ? 0 : err;
}

/*==============================================================================
  End of file
==============================================================================*/
