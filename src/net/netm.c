/*==============================================================================
File     netm.c

Author   Daniel Zorychta

Brief    Network management.

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

/*==============================================================================
  Include files
==============================================================================*/
#include "net/netm.h"
#include "cpuctl.h"
#include "kernel/sysfunc.h"
#include "net/cannet/cannet.h"
#include "net/inet/inet.h"
#include "net/sipc/sipc.h"
#include "net/uds/uds.h"

/*==============================================================================
  Local macros
==============================================================================*/
// clang-format off
#define PROXY_TABLE                             static const proxy_func_t proxy[_NETM_FAMILY__COUNT]
#define PROXY_TABLE_U16                         static const proxy_func_u16_t proxy[_NETM_FAMILY__COUNT]
#define PROXY_TABLE_U32                         static const proxy_func_u32_t proxy[_NETM_FAMILY__COUNT]
#define PROXY_TABLE_U64                         static const proxy_func_u64_t proxy[_NETM_FAMILY__COUNT]
#define PROXY_FUNCTION(_family, _proxy_func)    [NETM_FAMILY__##_family] = (proxy_func_t)_family##_##_proxy_func
#define PROXY_FUNCTION_U16(_family, _proxy_func)[NETM_FAMILY__##_family] = (proxy_func_u16_t)_family##_##_proxy_func
#define PROXY_FUNCTION_U32(_family, _proxy_func)[NETM_FAMILY__##_family] = (proxy_func_u32_t)_family##_##_proxy_func
#define PROXY_FUNCTION_U64(_family, _proxy_func)[NETM_FAMILY__##_family] = (proxy_func_u64_t)_family##_##_proxy_func
#define PROXY_ifinit(_family)                   PROXY_FUNCTION(_family, ifinit)
#define PROXY_ifdeinit(_family)                 PROXY_FUNCTION(_family, ifdeinit)
#define PROXY_ifup(_family)                     PROXY_FUNCTION(_family, ifup)
#define PROXY_ifdown(_family)                   PROXY_FUNCTION(_family, ifdown)
#define PROXY_ifstatus(_family)                 PROXY_FUNCTION(_family, ifstatus)
#define PROXY_gethostbyname(_family)            PROXY_FUNCTION(_family, gethostbyname)
#define PROXY_socket_create(_family)            PROXY_FUNCTION(_family, socket_create)
#define PROXY_socket_destroy(_family)           PROXY_FUNCTION(_family, socket_destroy)
#define PROXY_socket_bind(_family)              PROXY_FUNCTION(_family, socket_bind)
#define PROXY_socket_listen(_family)            PROXY_FUNCTION(_family, socket_listen)
#define PROXY_socket_accept(_family)            PROXY_FUNCTION(_family, socket_accept)
#define PROXY_socket_recv(_family)              PROXY_FUNCTION(_family, socket_recv)
#define PROXY_socket_recvfrom(_family)          PROXY_FUNCTION(_family, socket_recvfrom)
#define PROXY_socket_send(_family)              PROXY_FUNCTION(_family, socket_send)
#define PROXY_socket_sendto(_family)            PROXY_FUNCTION(_family, socket_sendto)
#define PROXY_socket_set_recv_timeout(_family)  PROXY_FUNCTION(_family, socket_set_recv_timeout)
#define PROXY_socket_set_send_timeout(_family)  PROXY_FUNCTION(_family, socket_set_send_timeout)
#define PROXY_socket_get_recv_timeout(_family)  PROXY_FUNCTION(_family, socket_get_recv_timeout)
#define PROXY_socket_get_send_timeout(_family)  PROXY_FUNCTION(_family, socket_get_send_timeout)
#define PROXY_socket_connect(_family)           PROXY_FUNCTION(_family, socket_connect)
#define PROXY_socket_disconnect(_family)        PROXY_FUNCTION(_family, socket_disconnect)
#define PROXY_socket_shutdown(_family)          PROXY_FUNCTION(_family, socket_shutdown)
#define PROXY_socket_getaddress(_family)        PROXY_FUNCTION(_family, socket_getaddress)
#define PROXY_hton_u16(_family)                 PROXY_FUNCTION_U16(_family, hton_u16)
#define PROXY_hton_u32(_family)                 PROXY_FUNCTION_U32(_family, hton_u32)
#define PROXY_hton_u64(_family)                 PROXY_FUNCTION_U64(_family, hton_u64)
#define call_proxy_function(family, ...)        (proxy[family] ? proxy[family](__VA_ARGS__) : ENOTSUP)
// clang-format on

/**
 * Family flag: the stack supports concurrent operations on a single socket.
 * Receiving operations (recv, recvfrom, accept) can be executed in parallel
 * with sending and control operations, and socket destroy aborts operations
 * that wait for data (destroy can be called while other thread is blocked
 * on the socket). Families without this flag use a single socket lock.
 */
#define FAMILY_FLAG__CONCURRENT (1 << 0)

/*==============================================================================
  Local object types
==============================================================================*/
struct netname {
        char *netname;
        void *stack_ctx;
        NETM_family_t family;
        size_t refs;
};

struct ksocket {
        res_header_t header;
        NETM_family_t family;
        kmtx_t *rx_mutex;    //!< serializes receiving operations (recv, recvfrom, accept)
        kmtx_t *tx_mutex;    //!< serializes sending and control operations (equal to rx_mutex if not concurrent)
        struct netname *net; //!< network interface
        void *ctx;           //!< stack socket object
        u16_t ops;           //!< number of operations in progress
        bool closing;        //!< socket is being destroyed
};

typedef int (*proxy_func_t)();
typedef u16_t (*proxy_func_u16_t)();
typedef u32_t (*proxy_func_u32_t)();
typedef u64_t (*proxy_func_u64_t)();

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local objects
==============================================================================*/
static struct netname net_list[16];
static kmtx_t *netm_mutex;

static const u8_t family_flags[_NETM_FAMILY__COUNT] = {
#if __ENABLE_TCPIP_STACK__ > 0
        [NETM_FAMILY__INET] = FAMILY_FLAG__CONCURRENT,
#endif
#if __ENABLE_UDS_STACK__ > 0
        [NETM_FAMILY__UDS] = FAMILY_FLAG__CONCURRENT,
#endif
};

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
 * @brief Function allocate socket.
 * @param socket        socket pointer
 * @param family        network family
 * @param net           network interface entry
 * @return One of @ref errno value.
 */
//==============================================================================
static int socket_alloc(ksocket_t **socket, NETM_family_t family, struct netname *net)
{
        static const uint8_t net_socket_size[_NETM_FAMILY__COUNT] = {
#if __ENABLE_TCPIP_STACK__ > 0
                [NETM_FAMILY__INET] = _mm_align(sizeof(INET_socket_t)),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                [NETM_FAMILY__SIPC] = _mm_align(sizeof(SIPC_socket_t)),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                [NETM_FAMILY__CANNET] = _mm_align(sizeof(CANNET_socket_t)),
#endif
#if __ENABLE_UDS_STACK__ > 0
                [NETM_FAMILY__UDS] = _mm_align(sizeof(UDS_socket_t)),
#endif
        };

        int err = _kzalloc(_MM_NET, _mm_align(sizeof(ksocket_t)) + net_socket_size[family], NULL, 0, 0,
                cast(void **, socket));

        if (!err) {
                ksocket_t *sock = *socket;

                err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &sock->rx_mutex);

                if (!err) {
                        if (family_flags[family] & FAMILY_FLAG__CONCURRENT) {
                                err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &sock->tx_mutex);
                        } else {
                                sock->tx_mutex = sock->rx_mutex;
                        }
                }

                if (!err) {
                        sock->header.self = sock;
                        sock->header.type = RES_TYPE_SOCKET;
                        sock->net = net;
                        sock->family = family;
                        sock->ctx = cast(void *, cast(size_t, sock) + _mm_align(sizeof(ksocket_t)));
                } else {
                        if (sock->rx_mutex) {
                                sys_mutex_destroy(sock->rx_mutex);
                        }

                        _kfree(_MM_NET, cast(void **, socket));
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function free allocated socket object.
 * @param socket        socket to free
 */
//==============================================================================
static void socket_free(ksocket_t **socket)
{
        (*socket)->header.self = NULL;
        (*socket)->header.type = RES_TYPE_UNKNOWN;

        if ((*socket)->tx_mutex != (*socket)->rx_mutex) {
                sys_mutex_destroy((*socket)->tx_mutex);
        }

        sys_mutex_destroy((*socket)->rx_mutex);

        (*socket)->rx_mutex = NULL;
        (*socket)->tx_mutex = NULL;

        _kfree(_MM_NET, cast(void **, socket));
        *socket = NULL;
}

//==============================================================================
/**
 * @brief Function check if socket object is valid.
 * @param socket        socket to validate
 * @return One of @ref errno value.
 */
//==============================================================================
static bool is_socket_valid(ksocket_t *socket)
{
        return _mm_is_object_in_heap(socket) && (socket->header.type == RES_TYPE_SOCKET) &&
                (socket->header.self == socket) && (socket->family < _NETM_FAMILY__COUNT) && (socket->net != NULL) &&
                (socket->rx_mutex) && (socket->tx_mutex) &&
                (socket->ctx == cast(void *, cast(size_t, socket) + _mm_align(sizeof(ksocket_t))));
}

//==============================================================================
/**
 * @brief Function starts socket operation. Operation is counted (socket is
 *        not released until operation ends) and selected socket lock is taken.
 * @param socket        socket
 * @param mutex         lock of operation (rx_mutex or tx_mutex)
 * @return One of @ref errno value. EBADF if socket is being destroyed.
 */
//==============================================================================
static int socket_op_begin(ksocket_t *socket, kmtx_t *mutex)
{
        int err = EBADF;

        sys_critical_section_begin();
        {
                if (not socket->closing) {
                        socket->ops++;
                        err = ESUCC;
                }
        }
        sys_critical_section_end();

        if (!err) {
                err = sys_mutex_lock(mutex, _WAIT_FOREVER_MS);

                // socket could be destroyed when operation waited for lock
                if (!err and socket->closing) {
                        sys_mutex_unlock(mutex);
                        err = EBADF;
                }

                if (err) {
                        sys_critical_section_begin();
                        socket->ops--;
                        sys_critical_section_end();
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function finishes socket operation started by socket_op_begin().
 * @param socket        socket
 * @param mutex         lock of operation (rx_mutex or tx_mutex)
 */
//==============================================================================
static void socket_op_end(ksocket_t *socket, kmtx_t *mutex)
{
        sys_mutex_unlock(mutex);

        sys_critical_section_begin();
        socket->ops--;
        sys_critical_section_end();
}

//==============================================================================
/**
 * @brief Function find network interface entry. Must be called with netm_mutex
 *        locked.
 * @param netname       network name
 * @return Interface entry or NULL if not found.
 */
//==============================================================================
static struct netname *find_net(const char *netname)
{
        for (size_t n = 0; n < ARRAY_SIZE(net_list); n++) {
                if (net_list[n].netname && isstreq(net_list[n].netname, netname)) {
                        return &net_list[n];
                }
        }

        return NULL;
}

//==============================================================================
/**
 * @brief Function find network interface entry and take a reference to it.
 *        Interface with references cannot be removed.
 * @param netname       network name
 * @param net           found interface entry
 * @return One of @ref errno value.
 */
//==============================================================================
static int net_get(const char *netname, struct netname **net)
{
        int err = sys_mutex_lock(netm_mutex, _WAIT_FOREVER_MS);
        if (!err) {
                *net = find_net(netname);
                if (*net) {
                        (*net)->refs++;
                } else {
                        err = ENODEV;
                }

                sys_mutex_unlock(netm_mutex);
        }

        return err;
}

//==============================================================================
/**
 * @brief Function take a next reference to the interface entry.
 * @param net           interface entry
 */
//==============================================================================
static void net_ref(struct netname *net)
{
        if (sys_mutex_lock(netm_mutex, _WAIT_FOREVER_MS) == 0) {
                net->refs++;
                sys_mutex_unlock(netm_mutex);
        }
}

//==============================================================================
/**
 * @brief Function release reference of interface entry.
 * @param net           interface entry
 */
//==============================================================================
static void net_put(struct netname *net)
{
        if (sys_mutex_lock(netm_mutex, _WAIT_FOREVER_MS) == 0) {
                if (net->refs > 0) {
                        net->refs--;
                }
                sys_mutex_unlock(netm_mutex);
        }
}

//==============================================================================
/**
 * @brief  Function initlialize network subsystem.
 *
 * @return One of errno value.
 */
//==============================================================================
int _net_init(void)
{
        return sys_mutex_create(KMTX_TYPE_RECURSIVE, &netm_mutex);
}

//==============================================================================
/**
 * @brief Function add stack .
 * @param netname       network name
 * @param family        network family
 * @param if_path       interface path (can be NULL)
 * @return Stack context.
 */
//==============================================================================
int _net_ifadd(const char *netname, NETM_family_t family, const char *if_path)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_ifinit(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_ifinit(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_ifinit(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_ifinit(UDS),
#endif
        };

        if (not netname or isstrempty(netname) or (family >= _NETM_FAMILY__COUNT)) {
                return EINVAL;
        }

        int err = ESUCC;

        if (not netm_mutex) {
                err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &netm_mutex);
                if (err) {
                        return err;
                }
        }

        err = sys_mutex_lock(netm_mutex, _WAIT_FOREVER_MS);
        if (!err) {

                if (find_net(netname) == NULL) {

                        size_t n = 0;
                        for (; n < ARRAY_SIZE(net_list); n++) {
                                if (net_list[n].netname == NULL) {
                                        break;
                                }
                        }

                        if (n < ARRAY_SIZE(net_list)) {
                                // network name is copied because caller's string can be released
                                char *name = NULL;
                                err = _kmalloc(_MM_NET, strlen(netname) + 1, NULL, 0, 0, cast(void **, &name));
                                if (!err) {
                                        strcpy(name, netname);

                                        err = call_proxy_function(family, &net_list[n].stack_ctx, if_path);
                                        if (!err) {
                                                net_list[n].netname = name;
                                                net_list[n].family = family;
                                                net_list[n].refs = 0;
                                        } else {
                                                net_list[n].stack_ctx = NULL;
                                                _kfree(_MM_NET, cast(void **, &name));
                                        }
                                }
                        } else {
                                err = ENOSPC;
                        }

                } else {
                        err = EADDRINUSE;
                }

                sys_mutex_unlock(netm_mutex);
        }

        return err;
}

//==============================================================================
/**
 * @brief Function remove network interface.
 * @param netname       network name
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_ifrm(const char *netname)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_ifdeinit(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_ifdeinit(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_ifdeinit(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_ifdeinit(UDS),
#endif
        };

        int err = EINVAL;

        if (netname) {
                err = sys_mutex_lock(netm_mutex, _WAIT_FOREVER_MS);
                if (!err) {

                        struct netname *net = find_net(netname);
                        if (net == NULL) {
                                err = ENODEV;

                        } else if (net->refs > 0) {
                                // interface is used by sockets or other operations
                                err = EBUSY;

                        } else {
                                err = call_proxy_function(net->family, net->stack_ctx);
                                if (!err) {
                                        _kfree(_MM_NET, cast(void **, &net->netname));
                                        net->netname = NULL;
                                        net->stack_ctx = NULL;
                                }
                        }

                        sys_mutex_unlock(netm_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function list network interfaces.
 * @param netname       pointer to array of network names
 * @param netname_len   size of destination array
 * @param count         number of interfaces
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_iflist(char *netname[], size_t netname_len, size_t *count)
{
        int err = EINVAL;

        if (netname && netname_len && count) {

                memset(netname, 0, netname_len * sizeof(*netname));

                err = sys_mutex_lock(netm_mutex, _WAIT_FOREVER_MS);
                if (!err) {

                        size_t n = 0;
                        size_t idx = 0;
                        for (size_t i = 0; i < ARRAY_SIZE(net_list); i++) {
                                if (net_list[i].netname && (idx < netname_len)) {
                                        netname[idx++] = net_list[i].netname;
                                        n++;
                                }
                        }

                        *count = n;

                        sys_mutex_unlock(netm_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function setup network interface.
 * @param netname       network name
 * @param config        configuration object (generic)
 * @param config_size   size of configuration structure
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_ifup(const char *netname, const NETM_generic_config_t *config, size_t config_size)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_ifup(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_ifup(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_ifup(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_ifup(UDS),
#endif
        };

        int err = EINVAL;

        if (netname) {
                struct netname *net;
                err = net_get(netname, &net);
                if (!err) {
                        err = call_proxy_function(net->family, net->stack_ctx, config, config_size);
                        net_put(net);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function shutdown network interface.
 * @param netname       network name
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_ifdown(const char *netname)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_ifdown(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_ifdown(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_ifdown(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_ifdown(UDS),
#endif
        };

        int err = EINVAL;

        if (netname) {
                struct netname *net;
                err = net_get(netname, &net);
                if (!err) {
                        err = call_proxy_function(net->family, net->stack_ctx);
                        net_put(net);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function return status of network interface.
 * @param  netname      network name
 * @param  family       network family
 * @param  status       network status
 * @param  status_size  size of status structure
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_ifstatus(const char *netname, NETM_family_t *family, NETM_generic_status_t *status, size_t status_size)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_ifstatus(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_ifstatus(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_ifstatus(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_ifstatus(UDS),
#endif
        };

        int err = EINVAL;

        if (netname && family) {
                struct netname *net;
                err = net_get(netname, &net);
                if (!err) {
                        *family = net->family;

                        if (status && status_size) {
                                err = call_proxy_function(net->family, net->stack_ctx, status, status_size);
                        }

                        net_put(net);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function create socket for specified network interface.
 * @param netname       network name
 * @param type          socket type
 * @param protocol      network protocol
 * @param socket        created socket
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_create(const char *netname, NETM_socket_type_t type, u8_t protocol, ksocket_t **socket)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_create(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_create(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_create(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_create(UDS),
#endif
        };

        int err = EINVAL;

        if (netname && (type < NETM_SOCKET_TYPE_COUNT) && socket) {

                struct netname *net;
                err = net_get(netname, &net);
                if (!err) {

                        err = socket_alloc(socket, net->family, net);

                        if (!err) {
                                err = call_proxy_function(net->family, net->stack_ctx, type, protocol, (*socket)->ctx);

                                if (err) {
                                        socket_free(socket);
                                }
                        }

                        // on success the reference is kept by socket
                        if (err) {
                                net_put(net);
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function destroy socket of specified network.
 * @param socket        socket to destroy
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_destroy(ksocket_t *socket)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_destroy(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_destroy(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_destroy(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_destroy(UDS),
#endif
        };

        if (not is_socket_valid(socket)) {
                return EINVAL;
        }

        bool closing;
        sys_critical_section_begin();
        {
                closing = socket->closing;
                socket->closing = true;
        }
        sys_critical_section_end();

        if (closing) {
                // socket is already destroyed by other thread
                return EBADF;
        }

        bool concurrent = family_flags[socket->family] & FAMILY_FLAG__CONCURRENT;

        int err = ESUCC;

        /*
         * Concurrent stack aborts operations that are in progress, so destroy
         * is called without lock. Other stacks are destroyed when operation in
         * progress is finished.
         */
        if (not concurrent) {
                err = sys_mutex_lock(socket->rx_mutex, _WAIT_FOREVER_MS);
        }

        if (!err) {
                err = call_proxy_function(socket->family, socket->net->stack_ctx, socket->ctx);

                if (not concurrent) {
                        sys_mutex_unlock(socket->rx_mutex);
                }
        }

        if (err) {
                sys_critical_section_begin();
                socket->closing = false;
                sys_critical_section_end();
                return err;
        }

        // wait for operations that exit (other threads can still use socket object)
        for (;;) {
                sys_critical_section_begin();
                u16_t ops = socket->ops;
                sys_critical_section_end();

                if (ops == 0) {
                        break;
                }

                sys_sleep_ms(1);
        }

        struct netname *net = socket->net;
        socket_free(&socket);
        net_put(net);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Function bind socket with address.
 * @param socket        socket to bind
 * @param addr          address to bind
 * @param addr_size     address size
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_bind(ksocket_t *socket, const NETM_generic_sockaddr_t *addr, size_t addr_size)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_bind(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_bind(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_bind(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_bind(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket) && addr && addr_size) {
                if ((err = socket_op_begin(socket, socket->tx_mutex)) == 0) {
                        err = call_proxy_function(socket->family, socket->net->stack_ctx, socket->ctx, addr, addr_size);
                        socket_op_end(socket, socket->tx_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function listen connection on socket.
 * @param socket        socket to listen
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_listen(ksocket_t *socket)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_listen(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_listen(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_listen(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_listen(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket)) {
                if ((err = socket_op_begin(socket, socket->tx_mutex)) == 0) {
                        err = call_proxy_function(socket->family, socket->net->stack_ctx, socket->ctx);
                        socket_op_end(socket, socket->tx_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function accept incoming connection.
 * @param socket        socket accepting connection
 * @param new_socket    socket of new connection
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_accept(ksocket_t *socket, ksocket_t **new_socket)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_accept(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_accept(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_accept(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_accept(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket) && new_socket && ((err = socket_op_begin(socket, socket->rx_mutex)) == 0)) {

                err = socket_alloc(new_socket, socket->family, socket->net);

                if (!err) {
                        err = call_proxy_function(socket->family, socket->net->stack_ctx, socket->ctx,
                                (*new_socket)->ctx);

                        if (err) {
                                socket_free(new_socket);
                        } else {
                                net_ref(socket->net);
                        }
                }

                socket_op_end(socket, socket->rx_mutex);
        }

        return err;
}

//==============================================================================
/**
 * @brief Function receive bytes from selected socket.
 * @param socket        socket to receive
 * @param buf           destination buffer
 * @param len           bytes to receive
 * @param flags         control flags
 * @param recved        number of received bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_recv(ksocket_t *socket, void *buf, size_t len, NETM_flags_t flags, size_t *recved)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_recv(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_recv(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_recv(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_recv(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket) && buf && len && recved) {
                if ((err = socket_op_begin(socket, socket->rx_mutex)) == 0) {
                        void *ctx = socket->net->stack_ctx;
                        err = call_proxy_function(socket->family, ctx, socket->ctx, buf, len, flags, recved);
                        socket_op_end(socket, socket->rx_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function receive bytes from selected socket and obtain sender address.
 * @param socket        socket to receive
 * @param buf           destination buffer
 * @param len           bytes to receive
 * @param flags         control flags
 * @param sockaddr      obtained address of received bytes
 * @param sockaddr_size address size
 * @param recved        number of received bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_recvfrom(ksocket_t *socket, void *buf, size_t len, NETM_flags_t flags,
        NETM_generic_sockaddr_t *sockaddr, size_t sockaddr_size, size_t *recved)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_recvfrom(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_recvfrom(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_recvfrom(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_recvfrom(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket) && buf && len && sockaddr && sockaddr_size && recved) {
                if ((err = socket_op_begin(socket, socket->rx_mutex)) == 0) {
                        void *ctx = socket->net->stack_ctx;
                        err = call_proxy_function(socket->family, ctx, socket->ctx, buf, len, flags, sockaddr,
                                sockaddr_size, recved);
                        socket_op_end(socket, socket->rx_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function send bytes to selected socket.
 * @param socket        socket that send bytes
 * @param buf           source buffer
 * @param len           number of bytes to send
 * @param flags         control flags
 * @param sent          number of sent bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_send(ksocket_t *socket, const void *buf, size_t len, NETM_flags_t flags, size_t *sent)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_send(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_send(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_send(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_send(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket) && buf && len && sent) {
                if ((err = socket_op_begin(socket, socket->tx_mutex)) == 0) {
                        void *ctx = socket->net->stack_ctx;
                        err = call_proxy_function(socket->family, ctx, socket->ctx, buf, len, flags, sent);
                        socket_op_end(socket, socket->tx_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function send bytes by socket to selected address.
 * @param socket        socket that send bytes
 * @param buf           source buffer
 * @param len           number of bytes to send
 * @param flags         control flags
 * @param to_addr       destination address
 * @param sent          number of sent bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_sendto(ksocket_t *socket, const void *buf, size_t len, NETM_flags_t flags,
        const NETM_generic_sockaddr_t *to_addr, size_t to_addr_size, size_t *sent)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_sendto(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_sendto(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_sendto(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_sendto(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket) && buf && len && to_addr && to_addr_size && sent) {
                if ((err = socket_op_begin(socket, socket->tx_mutex)) == 0) {
                        void *ctx = socket->net->stack_ctx;
                        err = call_proxy_function(socket->family, ctx, socket->ctx, buf, len, flags, to_addr,
                                to_addr_size, sent);
                        socket_op_end(socket, socket->tx_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function set socket receive timeout.
 * @param socket        socket
 * @param timeout       timeout value
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_set_recv_timeout(ksocket_t *socket, uint32_t timeout)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_set_recv_timeout(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_set_recv_timeout(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_set_recv_timeout(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_set_recv_timeout(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket)) {
                if ((err = socket_op_begin(socket, socket->tx_mutex)) == 0) {
                        void *ctx = socket->net->stack_ctx;
                        err = call_proxy_function(socket->family, ctx, socket->ctx, timeout);
                        socket_op_end(socket, socket->tx_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function set socket send timeout.
 * @param socket        socket
 * @param timeout       timeout value
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_set_send_timeout(ksocket_t *socket, uint32_t timeout)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_set_send_timeout(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_set_send_timeout(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_set_send_timeout(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_set_send_timeout(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket)) {
                if ((err = socket_op_begin(socket, socket->tx_mutex)) == 0) {
                        void *ctx = socket->net->stack_ctx;
                        err = call_proxy_function(socket->family, ctx, socket->ctx, timeout);
                        socket_op_end(socket, socket->tx_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function get socket receive timeout.
 * @param socket        socket
 * @param timeout       timeout value
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_get_recv_timeout(ksocket_t *socket, uint32_t *timeout)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_get_recv_timeout(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_get_recv_timeout(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_get_recv_timeout(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_get_recv_timeout(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket) && timeout) {
                if ((err = socket_op_begin(socket, socket->tx_mutex)) == 0) {
                        void *ctx = socket->net->stack_ctx;
                        err = call_proxy_function(socket->family, ctx, socket->ctx, timeout);
                        socket_op_end(socket, socket->tx_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function get socket send timeout.
 * @param socket        socket
 * @param timeout       timeout value
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_get_send_timeout(ksocket_t *socket, uint32_t *timeout)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_get_send_timeout(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_get_send_timeout(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_get_send_timeout(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_get_send_timeout(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket) && timeout) {
                if ((err = socket_op_begin(socket, socket->tx_mutex)) == 0) {
                        void *ctx = socket->net->stack_ctx;
                        err = call_proxy_function(socket->family, ctx, socket->ctx, timeout);
                        socket_op_end(socket, socket->tx_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function connect socket to selected address.
 * @param socket        socket used for connection
 * @param addr          address where socket is connecting
 * @param addr_size     address size
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_connect(ksocket_t *socket, const NETM_generic_sockaddr_t *addr, size_t addr_size)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_connect(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_connect(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_connect(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_connect(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket) && addr && addr_size) {
                if ((err = socket_op_begin(socket, socket->tx_mutex)) == 0) {
                        void *ctx = socket->net->stack_ctx;
                        err = call_proxy_function(socket->family, ctx, socket->ctx, addr, addr_size);
                        socket_op_end(socket, socket->tx_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function disconnect socket.
 * @param socket        socket to disconnect
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_disconnect(ksocket_t *socket)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_disconnect(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_disconnect(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_disconnect(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_disconnect(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket)) {
                if ((err = socket_op_begin(socket, socket->tx_mutex)) == 0) {
                        void *ctx = socket->net->stack_ctx;
                        err = call_proxy_function(socket->family, ctx, socket->ctx);
                        socket_op_end(socket, socket->tx_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function shutdown selected connection direction.
 * @param socket        socket to shutdown
 * @param how           direction to shutdown
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_shutdown(ksocket_t *socket, NETM_shut_t how)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_shutdown(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_shutdown(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_shutdown(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_shutdown(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket)) {
                if ((err = socket_op_begin(socket, socket->tx_mutex)) == 0) {
                        void *ctx = socket->net->stack_ctx;
                        err = call_proxy_function(socket->family, ctx, socket->ctx, how);
                        socket_op_end(socket, socket->tx_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function return address to which socket is connected.
 * @param socket        socket
 * @param sockaddr      address of connection
 * @param sockaddr_size size of address
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_socket_getaddress(ksocket_t *socket, NETM_generic_sockaddr_t *sockaddr, size_t sockaddr_size)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_socket_getaddress(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_socket_getaddress(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_socket_getaddress(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_socket_getaddress(UDS),
#endif
        };

        int err = EINVAL;

        if (is_socket_valid(socket) && sockaddr && sockaddr_size) {
                if ((err = socket_op_begin(socket, socket->tx_mutex)) == 0) {
                        void *ctx = socket->net->stack_ctx;
                        err = call_proxy_function(socket->family, ctx, socket->ctx, sockaddr, sockaddr_size);
                        socket_op_end(socket, socket->tx_mutex);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function return address of host by name.
 * @param netname       network name
 * @param name          host name
 * @param addr          obtained address
 * @param addr_size     size of address structure
 * @return One of @ref errno value.
 */
//==============================================================================
int _net_gethostbyname(const char *netname, const char *name, NETM_generic_sockaddr_t *addr, size_t addr_size)
{
        PROXY_TABLE = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_gethostbyname(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_gethostbyname(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_gethostbyname(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_gethostbyname(UDS),
#endif
        };

        int err = EINVAL;

        if (netname && name && addr) {
                struct netname *net;
                err = net_get(netname, &net);
                if (!err) {
                        err = call_proxy_function(net->family, net->stack_ctx, name, addr, addr_size);
                        net_put(net);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function convert host byte order to network.
 * @param family        network family
 * @param value         value to convert
 * @return Converted value.
 */
//==============================================================================
u16_t _net_hton_u16(NETM_family_t family, u16_t value)
{
        PROXY_TABLE_U16 = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_hton_u16(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_hton_u16(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_hton_u16(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_hton_u16(UDS),
#endif
        };

        if ((family < _NETM_FAMILY__COUNT) && proxy[family]) {
                return proxy[family](value);
        } else {
                return value;
        }
}

//==============================================================================
/**
 * @brief Function convert host byte order to network.
 * @param family        network family
 * @param value         value to convert
 * @return Converted value.
 */
//==============================================================================
u32_t _net_hton_u32(NETM_family_t family, u32_t value)
{
        PROXY_TABLE_U32 = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_hton_u32(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_hton_u32(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_hton_u32(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_hton_u32(UDS),
#endif
        };

        if ((family < _NETM_FAMILY__COUNT) && proxy[family]) {
                return proxy[family](value);
        } else {
                return value;
        }
}

//==============================================================================
/**
 * @brief Function convert host byte order to network.
 * @param family        network family
 * @param value         value to convert
 * @return Converted value.
 */
//==============================================================================
u64_t _net_hton_u64(NETM_family_t family, u64_t value)
{
        PROXY_TABLE_U64 = {
#if __ENABLE_TCPIP_STACK__ > 0
                PROXY_hton_u64(INET),
#endif
#if __ENABLE_SIPC_STACK__ > 0
                PROXY_hton_u64(SIPC),
#endif
#if __ENABLE_CANNET_STACK__ > 0
                PROXY_hton_u64(CANNET),
#endif
#if __ENABLE_UDS_STACK__ > 0
                PROXY_hton_u64(UDS),
#endif
        };

        if ((family < _NETM_FAMILY__COUNT) && proxy[family]) {
                return proxy[family](value);
        } else {
                return value;
        }
}

/*==============================================================================
  End of file
==============================================================================*/
