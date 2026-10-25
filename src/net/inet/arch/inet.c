/*==============================================================================
File     inet.c

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
#include "net/inet/inet.h"
#include "cpuctl.h"
#include "kernel/process.h"
#include "inet_types.h"
#include "lwip/api.h"
#include "lwip/dhcp.h"
#include "lwip/ip_addr.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/prot/dhcp.h"
#include "lwip/tcpip.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "netif/etharp.h"

/*==============================================================================
  Local macros
==============================================================================*/
#undef sys_mutex_lock
#undef sys_mutex_unlock

#define MAXIMUM_SAFE_UDP_PAYLOAD 1024

/*
 * Blocking socket operations wait in slices. After each slice the state of
 * socket (destroy, receive shutdown) and the process kill request are checked.
 * Slice does not delay data transfer (wait ends when data is available).
 */
#define WAIT_SLICE_MS 50

#define zalloc(_size, _pptr) _kzalloc(_MM_NET, _size, NULL, _MM_FLAG__DMA, _MM_FLAG__DMA, _pptr)
#define zfree(_pptr)         _kfree(_MM_NET, _pptr)

/*
 * lwIP core functions (netif, DHCP, DNS, PCB) must be called with the TCPIP
 * core lock taken (LWIP_TCPIP_CORE_LOCKING). The LOCK_TCPIP_CORE() macro can
 * not be used because sys_mutex_lock is redefined above to the kernel function.
 */
#define core_lock()   lwip_sys_mutex_lock(&lock_tcpip_core)
#define core_unlock() lwip_sys_mutex_unlock(&lock_tcpip_core)

/*==============================================================================
  Local object types
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int network_interface_thread(void *sem);
static void clear_rx_tx_counters(inet_t *inet);
static bool is_mac_set(inet_t *inet);
static bool wait_for_link(inet_t *inet);
static dhcp_state_enum_t DHCP_get_state(inet_t *inet);
static int DHCP_start_client(inet_t *inet);
static err_t netif_configure(struct netif *netif);
static int load_dns_file(inet_t *inet);
static int apply_static_IP_configuration(inet_t *inet, const ip_addr_t *ip_address, const ip_addr_t *net_mask,
        const ip_addr_t *gateway);
static int send_datagram(INET_socket_t *inet_sock, const void *buf, size_t len, NETM_flags_t flags,
        const ip_addr_t *addr, u16_t port, size_t *sent);
static int send_stream(INET_socket_t *inet_sock, const void *buf, size_t len, NETM_flags_t flags, size_t *sent);
static bool sock_op_begin(INET_socket_t *inet_sock, bool rx);
static void sock_op_end(INET_socket_t *inet_sock, bool rx);
static void sock_wait_idle(INET_socket_t *inet_sock, bool tx);
static bool next_slice(clock_t tref, u32_t timeout, u32_t *slice);
static int sock_abort_check(INET_socket_t *inet_sock, bool rx);
static int sliced_receive(INET_socket_t *inet_sock, struct netbuf **netbuf, struct netconn **new_conn);
static void netconn_event(struct netconn *conn, enum netconn_evt evt, u16_t len);
static int socket_init(INET_socket_t *inet_sock);

/*==============================================================================
  External function prototypes
==============================================================================*/
extern err_t _inetdrv_handle_output(struct netif *netif, struct pbuf *p);
extern void _inetdrv_handle_input(inet_t *inet, u32_t timeout);
extern bool _inetdrv_is_link_connected(inet_t *inet);
extern int _inetdrv_get_MAC(inet_t *inet, u8_t MAC[6]);

/*==============================================================================
  Local objects
==============================================================================*/
static bool tcp_initialized;
static const u32_t ACCESS_TIMEOUT = 10000;
static const u32_t DHCP_TIMEOUT = 5000;
static const u32_t INIT_TIMEOUT = 5000;
static const u32_t INPUT_TIMEOUT = 1000;
static const u32_t LINK_POLL_TIME = 250;
static const u32_t THREAD_EXIT_TIMEOUT = 5000;

static const _thread_attr_t if_thread_attr = {
        .priority = _PRIORITY_NORMAL,
        .stack_depth = _STACK_DEPTH_LOW,
        .detached = true,
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
 * @brief  Clear received and transmitted byte counters
 * @param  None
 * @return None
 */
//==============================================================================
static void clear_rx_tx_counters(inet_t *inet)
{
        inet->rx_bytes = 0;
        inet->tx_bytes = 0;
        inet->rx_packets = 0;
        inet->tx_packets = 0;
}

//==============================================================================
/**
 * @brief  Function wait for the link to be connected (interface ready).
 *
 * @param  inet         inet instance
 *
 * @return Returns true if link is ready, otherwise false (timeout).
 */
//==============================================================================
static bool wait_for_link(inet_t *inet)
{
        clock_t tref = sys_get_uptime_ms();
        while (not inet->ready && not sys_is_time_expired(tref, INIT_TIMEOUT)) {
                sys_sleep_ms(10);
        }

        return inet->ready;
}

//==============================================================================
/**
 * @brief  Check if MAC address is set.
 *
 * @param  inet         inet instance
 *
 * @return True if MAC set, otherwise false.
 */
//==============================================================================
static bool is_mac_set(inet_t *inet)
{
        for (size_t i = 0; i < ARRAY_SIZE(inet->netif.hwaddr); i++) {
                if (inet->netif.hwaddr[i]) {
                        return true;
                }
        }

        return false;
}

//==============================================================================
/**
 * @brief Network interface thread
 *
 * The thread monitors the link state of the interface and receives incoming
 * packets. When the link is connected then the MAC address is read from
 * the interface and the lwIP interface link is set up. When the link is
 * disconnected then the lwIP interface link is set down. The IP configuration
 * (static address or DHCP) is kept by lwIP during link down, so it is
 * restored automatically when the link is connected again (DHCP is restarted
 * by lwIP).
 *
 * @param  arg          inet instance
 *
 * @return Always 0.
 */
//==============================================================================
static int network_interface_thread(void *arg)
{
        inet_t *inet = arg;

        bool link_up = false;

        while (inet->thread_run) {

                bool link = _inetdrv_is_link_connected(inet);

                if (link and not link_up) {
                        core_lock();
                        _inetdrv_get_MAC(inet, inet->netif.hwaddr);

                        if (is_mac_set(inet)) {
                                netif_set_link_up(&inet->netif);
                                link_up = true;
                        }
                        core_unlock();

                        if (link_up) {
                                inet->disconnected = false;
                                inet->ready = true;
                                printk("INET: Link connected");
                        }

                } else if (not link and link_up) {
                        inet->ready = false;
                        inet->disconnected = true;
                        link_up = false;

                        core_lock();
                        netif_set_link_down(&inet->netif);
                        core_unlock();

                        printk("INET: Link disconnected");
                }

                if (link_up) {
                        _inetdrv_handle_input(inet, INPUT_TIMEOUT);
                } else {
                        sys_sleep_ms(LINK_POLL_TIME);
                }
        }

        sys_semaphore_signal(inet->thread_exit);

        return 0;
}

//==============================================================================
/**
 * @brief Function create NETM_INET_IPv4_t address from lwIP address.
 * @param addr          INET address
 * @param lwip_addr     lwIP address
 */
//==============================================================================
static void create_addr(NETM_INET_IPv4_t *addr, const ip_addr_t *lwip_addr)
{
        *addr = NETM_INET_IPv4(ip4_addr1(lwip_addr), ip4_addr2(lwip_addr), ip4_addr3(lwip_addr), ip4_addr4(lwip_addr));
}

//==============================================================================
/**
 * @brief Function create lwIP address by using INET address.
 * @param lwip_addr     lwIP address
 * @param addr          INET address
 */
//==============================================================================
static void create_lwIP_addr(ip_addr_t *lwip_addr, const NETM_INET_IPv4_t *addr)
{
        IP4_ADDR(lwip_addr, NETM_INET_IPv4_a(*addr), NETM_INET_IPv4_b(*addr), NETM_INET_IPv4_c(*addr),
                NETM_INET_IPv4_d(*addr));
}

//==============================================================================
/**
 * @brief  Function configure interface as static
 * @param  ip_address        a IP address
 * @param  net_mask          a net mask value
 * @param  gateway           a gateway address
 * @return One of @ref errno value.
 */
//==============================================================================
static int apply_static_IP_configuration(inet_t *inet, const ip_addr_t *ip_address, const ip_addr_t *net_mask,
        const ip_addr_t *gateway)
{
        int status = EINVAL;

        if (inet && ip_address && net_mask && gateway && !netif_is_up(&inet->netif) &&
                sys_mutex_lock(inet->access, ACCESS_TIMEOUT) == ESUCC) {

                clear_rx_tx_counters(inet);

                core_lock();
                netif_set_addr(&inet->netif, const_cast(ip_address), const_cast(net_mask), const_cast(gateway));
                netif_set_up(&inet->netif);
                core_unlock();

                inet->configured = true;

                status = ESUCC;

                sys_mutex_unlock(inet->access);
        }

        return status;
}

//==============================================================================
/**
 * @brief  Load DNS list file.
 * @param  inet         inet instance
 * @return One of @ref errno value.
 */
//==============================================================================
static int load_dns_file(inet_t *inet)
{
        UNUSED_ARG1(inet);

        if (isstrempty(__NETWORK_LWIP_DNS_LIST_PATH__)) {
                return 0;
        }

        kfile_t *dns_file;
        int err = sys_fopen(__NETWORK_LWIP_DNS_LIST_PATH__, O_RDONLY, 0, &dns_file);
        if (err) {
                printk("INET: no such DNS file: '%s'", __NETWORK_LWIP_DNS_LIST_PATH__);
                return err;
        }

        char buf[128] = {0};
        size_t rdcnt;
        if (sys_fread(buf, sizeof(buf) - 1, &rdcnt, dns_file) == 0) {

                char *ptr = buf;
                u8_t numdns = 0;

                while (*ptr and (numdns < DNS_MAX_SERVERS)) {
                        uint a = 0, b = 0, c = 0, d = 0;

                        if (sys_sscanf(ptr, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
                                printk("INET: added DNS: %u.%u.%u.%u", a, b, c, d);

                                ip_addr_t dns;
                                IP_ADDR4(&dns, a, b, c, d);

                                core_lock();
                                dns_setserver(numdns++, &dns);
                                core_unlock();
                        }

                        ptr = strchr(ptr, '\n');
                        if (ptr == NULL) {
                                break;
                        } else {
                                ptr++;
                        }
                };
        }

        sys_fclose(dns_file);

        return err;
}

//==============================================================================
/**
 * @brief  Function return DHCP state.
 * @return DHCP state.
 */
//==============================================================================
static dhcp_state_enum_t DHCP_get_state(inet_t *inet)
{
        struct dhcp *dhcp = inet->netif.client_data[LWIP_NETIF_CLIENT_DATA_INDEX_DHCP];
        if (dhcp) {
                return dhcp->state;

        } else {
                return DHCP_STATE_OFF;
        }
}

//==============================================================================
/**
 * @brief  Function starts DHCP client.
 *
 * The DHCP client is started even if the link is disconnected (lwIP starts
 * discovery when link is connected). The function waits for the link and for
 * the address. If address is not supplied in time then the DHCP client still
 * works in the background (interface is in DHCP configuring state) and ETIME
 * is returned.
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static int DHCP_start_client(inet_t *inet)
{
        if (not inet) {
                return ENONET;
        }

        if (netif_is_up(&inet->netif)) {
                return EADDRINUSE;
        }

        int err = sys_mutex_lock(inet->access, ACCESS_TIMEOUT);
        if (err) {
                return err;
        }

        clear_rx_tx_counters(inet);

        core_lock();
        netif_set_addr(&inet->netif, const_cast(&ip_addr_any), const_cast(&ip_addr_any), const_cast(&ip_addr_any));
        netif_set_up(&inet->netif);

        err_t lwip_err = dhcp_start(&inet->netif);
        if (lwip_err != ERR_OK) {
                netif_set_down(&inet->netif);
        }
        core_unlock();

        if (lwip_err == ERR_OK) {
                inet->configured = true;

                err = ETIME;

                if (wait_for_link(inet)) {
                        clock_t tref = sys_get_uptime_ms();

                        while (not sys_is_time_expired(tref, DHCP_TIMEOUT)) {
                                if (dhcp_supplied_address(&inet->netif)) {
                                        err = ESUCC;
                                        break;
                                } else {
                                        sys_sleep_ms(100);
                                }
                        }
                }
        } else {
                err = err_to_errno(lwip_err);
        }

        sys_mutex_unlock(inet->access);

        return err;
}

//==============================================================================
/**
 * @brief  Function inform DHCP about current static configuration
 * @return One of @ref errno value.
 */
//==============================================================================
static int DHCP_inform_server(inet_t *inet)
{
        int status = ENONET;

        if (inet && netif_is_up(&inet->netif) && (DHCP_get_state(inet) == DHCP_STATE_OFF) &&
                sys_mutex_lock(inet->access, ACCESS_TIMEOUT) == ESUCC) {

                core_lock();
                dhcp_inform(&inet->netif);
                core_unlock();

                status = ESUCC;

                sys_mutex_unlock(inet->access);
        }

        return status;
}

//==============================================================================
/**
 * @brief  Function renew DHCP connection
 * @return One of @ref errno value.
 */
//==============================================================================
static int DHCP_renew_connection(inet_t *inet)
{
        int status = ENONET;

        if (inet && netif_is_up(&inet->netif) && (DHCP_get_state(inet) != DHCP_STATE_OFF) &&
                sys_mutex_lock(inet->access, ACCESS_TIMEOUT) == ESUCC) {

                core_lock();
                err_t lwip_err = dhcp_renew(&inet->netif);
                core_unlock();

                if (lwip_err == ERR_OK) {

                        clock_t tref = sys_get_uptime_ms();
                        while (not sys_is_time_expired(tref, DHCP_TIMEOUT)) {

                                if (DHCP_get_state(inet) == DHCP_STATE_BOUND) {
                                        status = ESUCC;
                                        break;
                                } else {
                                        sys_sleep_ms(500);
                                }
                        }
                }

                sys_mutex_unlock(inet->access);
        }

        return status;
}

//==============================================================================
/**
 * @brief  Function configures network interface (TCPIP stack configuration).
 *         The function must not allocate any memory and block program flow.
 *
 * @param  netif        the lwip network interface structure for this interface
 *
 * @return ERR_OK       if the loopif is initialized
 *         ERR_MEM      if private data couldn't be allocated any other err_t on error
 *
 * @note   Called at system startup.
 */
//==============================================================================
static err_t netif_configure(struct netif *netif)
{
        netif->hostname = const_cast(__OS_HOSTNAME__);
        netif->name[0] = 'E';
        netif->name[1] = 'T';
        netif->output = etharp_output;
        netif->linkoutput = _inetdrv_handle_output;
        netif->mtu = 1500;
        netif->hwaddr_len = ETHARP_HWADDR_LEN;
        netif->flags = NETIF_FLAG_ETHERNET | NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;

        return ERR_OK;
}

//==============================================================================
/**
 * @brief  Function initialize network.
 * @param  ctx          context
 * @param  if_path      interface path
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_ifinit(void **ctx, const char *if_path)
{
        if (not tcp_initialized) {
                printk("TCP/IP stack: lwIP v%u.%u.%u", LWIP_VERSION_MAJOR, LWIP_VERSION_MINOR, LWIP_VERSION_REVISION);
                tcpip_init(NULL, NULL);
                tcp_initialized = true;
        }

        int err = zalloc(sizeof(inet_t), ctx);
        if (err) {
                return err;
        }

        inet_t *inet = *ctx;

        inet->disconnected = true;

        err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &inet->access);
        if (err) {
                goto finish;
        }

        err = sys_semaphore_create(1, 0, &inet->thread_exit);
        if (err) {
                goto finish;
        }

        err = sys_fopen(if_path, O_RDWR, 0, &inet->if_file);
        if (err) {
                goto finish;
        }

        core_lock();
        {
                if (netif_add(&inet->netif, const_cast(&ip_addr_any), const_cast(&ip_addr_any),
                            const_cast(&ip_addr_any), inet, netif_configure, tcpip_input)) {

                        inet->netif_added = true;

                        if (netif_default == NULL) {
                                netif_set_default(&inet->netif);
                        }
                } else {
                        err = EIO;
                }
        }
        core_unlock();

        if (err) {
                goto finish;
        }

        inet->thread_run = true;

        err = sys_thread_create(network_interface_thread, &if_thread_attr, inet, &inet->if_thread);
        if (err) {
                inet->thread_run = false;
                goto finish;
        }

        printk("INET (%s): thread ID: %u", if_path, inet->if_thread);

finish:
        if (err) {
                INET_ifdeinit(inet);
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
int INET_ifdeinit(void *ctx)
{
        inet_t *inet = ctx;

        if (not inet) {
                return EINVAL;
        }

        if (inet->thread_run) {
                inet->thread_run = false;

                if (sys_semaphore_wait(inet->thread_exit, THREAD_EXIT_TIMEOUT) != ESUCC) {
                        printk("INET: interface thread does not respond, killed");
                        sys_thread_destroy(inet->if_thread);
                }
        }

        if (inet->netif_added) {
                core_lock();
                {
                        if (DHCP_get_state(inet) != DHCP_STATE_OFF) {
                                dhcp_release_and_stop(&inet->netif);
                        }

                        dhcp_cleanup(&inet->netif);
                        netif_set_down(&inet->netif);
                        netif_remove(&inet->netif);
                }
                core_unlock();

                inet->netif_added = false;
        }

        if (inet->if_file) {
                sys_fclose(inet->if_file);
        }

        if (inet->thread_exit) {
                sys_semaphore_destroy(inet->thread_exit);
        }

        if (inet->access) {
                sys_mutex_destroy(inet->access);
        }

        return zfree(&ctx);
}

//==============================================================================
/**
 * @brief  Function up the network.
 * @param  cfg          configuration structure
 * @oaram  cfg_size     configuration size
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_ifup(void *ctx, const NETM_INET_config_t *cfg, size_t cfg_size)
{
        inet_t *inet = ctx;

        if (not inet or not cfg or (cfg_size != sizeof(*cfg))) {
                return EINVAL;
        }

        int err = EINVAL;

        switch (cfg->mode) {
        case NETM_INET_MODE__STATIC: {
                ip_addr_t addr, mask, gateway;
                create_lwIP_addr(&addr, &cfg->address);
                create_lwIP_addr(&mask, &cfg->mask);
                create_lwIP_addr(&gateway, &cfg->gateway);

                load_dns_file(inet);

                err = apply_static_IP_configuration(inet, &addr, &mask, &gateway);

                break;
        }

        case NETM_INET_MODE__DHCP_START:
                err = DHCP_start_client(inet);
                break;

        case NETM_INET_MODE__DHCP_INFORM:
                err = DHCP_inform_server(inet);
                break;

        case NETM_INET_MODE__DHCP_RENEW:
                err = DHCP_renew_connection(inet);
                break;

        default:
                err = EINVAL;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function turn down interface with static configuration.
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_ifdown(void *ctx)
{
        inet_t *inet = ctx;

        int status = ENONET;

        if (inet && netif_is_up(&inet->netif) && sys_mutex_lock(inet->access, ACCESS_TIMEOUT) == ESUCC) {

                core_lock();
                {
                        if ((DHCP_get_state(inet) != DHCP_STATE_OFF)) {
                                dhcp_release_and_stop(&inet->netif);
                        }

                        netif_set_down(&inet->netif);
                }
                core_unlock();

                inet->configured = false;
                status = ESUCC;

                sys_mutex_unlock(inet->access);
        }

        return status;
}

//==============================================================================
/**
 * @brief  Function returns interface status.
 * @param  status       status container
 * @param  status_size  status size (at least)
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_ifstatus(void *ctx, NETM_INET_status_t *status, size_t status_size)
{
        if (status_size < sizeof(*status)) {
                return EINVAL;
        }

        inet_t *inet = ctx;

        int err = ENONET;

        if (not inet) {
                return err;
        }

        memcpy(status->hw_addr, inet->netif.hwaddr, sizeof(status->hw_addr));

        status->address = NETM_INET_IPv4_ANY;
        status->mask = NETM_INET_IPv4_ANY;
        status->gateway = NETM_INET_IPv4_ANY;

        status->rx_packets = inet->rx_packets;
        status->rx_bytes = inet->rx_bytes;
        status->tx_packets = inet->tx_packets;
        status->tx_bytes = inet->tx_bytes;

        status->state = NETM_INET_STATE__NOT_CONFIGURED;

        if (inet->configured) {
                if (inet->disconnected) {
                        status->state = NETM_INET_STATE__LINK_DISCONNECTED;

                } else if (netif_is_up(&inet->netif)) {
                        if ((DHCP_get_state(inet) != DHCP_STATE_OFF)) {
                                if (DHCP_get_state(inet) != DHCP_STATE_BOUND) {
                                        status->state = NETM_INET_STATE__DHCP_CONFIGURING;
                                } else {
                                        status->state = NETM_INET_STATE__DHCP_CONFIGURED;
                                }
                        } else {
                                status->state = NETM_INET_STATE__STATIC_IP;
                        }

                        create_addr(&status->address, &inet->netif.ip_addr);
                        create_addr(&status->mask, &inet->netif.netmask);
                        create_addr(&status->gateway, &inet->netif.gw);
                }
        } else {
                if (inet->disconnected) {
                        status->state = NETM_INET_STATE__LINK_DISCONNECTED;
                }
        }

        err = ESUCC;

        return err;
}

//==============================================================================
/**
 * @brief  Function starts operation that calls lwIP functions of socket.
 *
 * Operations are counted, so socket destroy and receive shutdown can wait
 * until no thread uses the netconn object. Without LWIP_NETCONN_FULLDUPLEX
 * lwIP does not allow to close netconn (or shutdown receive direction) when
 * other thread waits on it.
 *
 * @param  inet_sock    socket
 * @param  rx           receiving operation
 *
 * @return True if operation can be started, false if socket is closing.
 */
//==============================================================================
static bool sock_op_begin(INET_socket_t *inet_sock, bool rx)
{
        bool ok = false;

        sys_critical_section_begin();
        {
                if (not inet_sock->closing and not(rx and inet_sock->rd_shut)) {
                        if (rx) {
                                inet_sock->rx_active++;
                        } else {
                                inet_sock->tx_active++;
                        }

                        ok = true;
                }
        }
        sys_critical_section_end();

        return ok;
}

//==============================================================================
/**
 * @brief  Function finishes operation started by sock_op_begin().
 *
 * @param  inet_sock    socket
 * @param  rx           receiving operation
 */
//==============================================================================
static void sock_op_end(INET_socket_t *inet_sock, bool rx)
{
        sys_critical_section_begin();
        {
                if (rx) {
                        inet_sock->rx_active--;
                } else {
                        inet_sock->tx_active--;
                }
        }
        sys_critical_section_end();
}

//==============================================================================
/**
 * @brief  Function waits until selected operations leave lwIP functions.
 *         Blocking operations are executed in slices, so waiting takes at
 *         most one slice (except of TCP connect which waits for result).
 *
 * @param  inet_sock    socket
 * @param  tx           wait also for not receiving operations
 */
//==============================================================================
static void sock_wait_idle(INET_socket_t *inet_sock, bool tx)
{
        for (;;) {
                sys_critical_section_begin();
                bool busy = (inet_sock->rx_active > 0) or (tx and (inet_sock->tx_active > 0));
                sys_critical_section_end();

                if (not busy) {
                        break;
                }

                sys_sleep_ms(1);
        }
}

//==============================================================================
/**
 * @brief  Function calculates next wait slice of operation.
 *
 * @param  tref         operation start time
 * @param  timeout      operation timeout (_WAIT_FOREVER_MS is infinite)
 * @param  slice        calculated slice
 *
 * @return True if slice is available, false if operation timeout expired.
 */
//==============================================================================
static bool next_slice(clock_t tref, u32_t timeout, u32_t *slice)
{
        *slice = WAIT_SLICE_MS;

        if (timeout != _WAIT_FOREVER_MS) {
                clock_t elapsed = sys_get_uptime_ms() - tref;
                if (elapsed >= timeout) {
                        return false;
                }

                *slice = min(*slice, timeout - elapsed);
        }

        return true;
}

//==============================================================================
/**
 * @brief  Function returns error when blocking operation should be aborted.
 *
 * @param  inet_sock    socket
 * @param  rx           receiving operation
 *
 * @return One of @ref errno value, ESUCC if operation can continue.
 */
//==============================================================================
static int sock_abort_check(INET_socket_t *inet_sock, bool rx)
{
        if (_process_is_kill_requested()) {
                return ECANCELED;
        }

        if (inet_sock->closing) {
                return ECONNABORTED;
        }

        if (rx and inet_sock->rd_shut) {
                return err_to_errno(ERR_CONN);
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function receives netbuf (receive or accept) in slices. Between
 *         slices it checks if socket is not closing and if process is not
 *         killed.
 *
 * @param  inet_sock    socket
 * @param  netbuf       received buffer (NULL for accept)
 * @param  new_conn     accepted connection (NULL for receive)
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static int sliced_receive(INET_socket_t *inet_sock, struct netbuf **netbuf, struct netconn **new_conn)
{
        clock_t tref = sys_get_uptime_ms();
        u32_t timeout = inet_sock->recv_timeout;
        bool first = true;

        for (;;) {
                int err = sock_abort_check(inet_sock, true);
                if (err) {
                        return err;
                }

                u32_t slice;
                if (not next_slice(tref, timeout, &slice)) {
                        if (first) {
                                // zero timeout: check once if data is available
                                slice = 1;
                        } else {
                                return err_to_errno(ERR_TIMEOUT);
                        }
                }

                first = false;

                if (not sock_op_begin(inet_sock, true)) {
                        err = sock_abort_check(inet_sock, true);
                        return err ? err : ECONNABORTED;
                }

                // recv timeout is set only by receiving side (serialized by netm)
                netconn_set_recvtimeout(inet_sock->netconn, max(slice, 1));

                err_t lwip_err;
                if (new_conn) {
                        lwip_err = netconn_accept(inet_sock->netconn, new_conn);
                } else {
                        lwip_err = netconn_recv(inet_sock->netconn, netbuf);
                }

                sock_op_end(inet_sock, true);

                if (lwip_err != ERR_TIMEOUT) {
                        return err_to_errno(lwip_err);
                }
        }
}

//==============================================================================
/**
 * @brief  Function called by lwIP when netconn event occurs. Function is
 *         called from TCPIP thread or from thread that holds core lock.
 *
 * @param  conn         netconn
 * @param  evt          event
 * @param  len          length of data
 */
//==============================================================================
static void netconn_event(struct netconn *conn, enum netconn_evt evt, u16_t len)
{
        UNUSED_ARG1(len);

        if ((evt == NETCONN_EVT_SENDPLUS) or (evt == NETCONN_EVT_ERROR)) {
                // callback argument is not set for connection that is not accepted yet
                if (conn->callback_arg.socket != -1) {
                        INET_socket_t *inet_sock = netconn_get_callback_arg(conn);
                        if (inet_sock and inet_sock->tx_event) {
                                sys_semaphore_signal(inet_sock->tx_event);
                        }
                }
        }
}

//==============================================================================
/**
 * @brief  Function initializes socket object for created or accepted netconn.
 *
 * @param  inet_sock    socket (netconn must be set)
 *
 * @return One of @ref errno value.
 */
//==============================================================================
static int socket_init(INET_socket_t *inet_sock)
{
        int err = sys_semaphore_create(1, 0, &inet_sock->tx_event);
        if (!err) {
                inet_sock->recv_timeout = _WAIT_FOREVER_MS;
                inet_sock->send_timeout = _WAIT_FOREVER_MS;

                core_lock();
                netconn_set_callback_arg(inet_sock->netconn, inet_sock);

                if ((NETCONNTYPE_GROUP(netconn_type(inet_sock->netconn)) == NETCONN_TCP) &&
                        inet_sock->netconn->pcb.tcp) {
                        tcp_nagle_disable(inet_sock->netconn->pcb.tcp);
                }
                core_unlock();
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function create socket container.
 * @param  type         socket type
 * @param  prot         protocol
 * @param  inet_sock    inet socket
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_create(void *ctx, NETM_socket_type_t type, u8_t prot, INET_socket_t *inet_sock)
{
        UNUSED_ARG1(ctx);

        if (type >= NETM_SOCKET_TYPE_COUNT) {
                return EINVAL;
        }

        static const enum netconn_type LWIP_TYPE[] = {
                [NETM_SOCKET_TYPE__RAW] = NETCONN_RAW,
                [NETM_SOCKET_TYPE__DATAGRAM] = NETCONN_UDP,
                [NETM_SOCKET_TYPE__STREAM] = NETCONN_TCP,
        };

        inet_sock->netconn = netconn_new_with_proto_and_callback(LWIP_TYPE[type], prot, netconn_event);
        if (not inet_sock->netconn) {
                return ENOMEM;
        }

        int err = socket_init(inet_sock);
        if (err) {
                netconn_delete(inet_sock->netconn);
                inet_sock->netconn = NULL;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function destroy created socket. Function can be called when other
 *         thread uses the socket: blocking operations are aborted and the
 *         netconn is deleted when no thread uses it.
 * @param  inet_sock    inet socket to destroy
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_destroy(void *ctx, INET_socket_t *inet_sock)
{
        UNUSED_ARG1(ctx);

        sys_critical_section_begin();
        inet_sock->closing = true;
        sys_critical_section_end();

        if (inet_sock->tx_event) {
                // wake up sender that waits for send space
                sys_semaphore_signal(inet_sock->tx_event);
        }

        sock_wait_idle(inet_sock, true);

        if (inet_sock->netconn) {
                core_lock();
                netconn_set_callback_arg(inet_sock->netconn, NULL);
                core_unlock();

                netconn_close(inet_sock->netconn);
                netconn_delete(inet_sock->netconn);
                inet_sock->netconn = NULL;
        }

        if (inet_sock->netbuf_recv) {
                netbuf_delete(inet_sock->netbuf_recv);
                inet_sock->netbuf_recv = NULL;
        }

        if (inet_sock->netbuf_send) {
                netbuf_delete(inet_sock->netbuf_send);
                inet_sock->netbuf_send = NULL;
        }

        if (inet_sock->tx_event) {
                sys_semaphore_destroy(inet_sock->tx_event);
                inet_sock->tx_event = NULL;
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function connect socket to selected address.
 * @param  inet_sock    socket
 * @param  addr         inet address
 * @param  addr_size    address size
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_connect(void *ctx, INET_socket_t *inet_sock, const NETM_INET_sockaddr_t *addr, size_t addr_size)
{
        UNUSED_ARG1(ctx);

        if (addr_size != sizeof(*addr)) {
                return EINVAL;
        }

        if (not sock_op_begin(inet_sock, false)) {
                return ECONNABORTED;
        }

        ip_addr_t IP;
        create_lwIP_addr(&IP, &addr->addr);

        int err = err_to_errno(netconn_connect(inet_sock->netconn, &IP, addr->port));

        sock_op_end(inet_sock, false);

        return err;
}

//==============================================================================
/**
 * @brief  Function disconnect socket from selected address.
 * @param  inet_sock     socket
 * @param  addr          inet addres
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_disconnect(void *ctx, INET_socket_t *inet_sock)
{
        UNUSED_ARG1(ctx);

        if (not sock_op_begin(inet_sock, false)) {
                return ECONNABORTED;
        }

        int err = err_to_errno(netconn_disconnect(inet_sock->netconn));

        sock_op_end(inet_sock, false);

        return err;
}

//==============================================================================
/**
 * @brief  Function shutdown selected socket direction.
 * @param  inet_sock    socket
 * @param  how          shutdown direction
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_shutdown(void *ctx, INET_socket_t *inet_sock, NETM_shut_t how)
{
        UNUSED_ARG1(ctx);

        if (how & NETM_SHUT__RD) {
                /*
                 * lwIP releases receive mailbox of netconn at receive shutdown,
                 * so no thread can wait on it. Receiving thread is aborted
                 * (it exits within one wait slice).
                 */
                sys_critical_section_begin();
                inet_sock->rd_shut = true;
                sys_critical_section_end();

                sock_wait_idle(inet_sock, false);
        }

        if (not sock_op_begin(inet_sock, false)) {
                return ECONNABORTED;
        }

        int err = err_to_errno(netconn_shutdown(inet_sock->netconn, how & NETM_SHUT__RD, how & NETM_SHUT__WR));

        sock_op_end(inet_sock, false);

        if (how & NETM_SHUT__WR) {
                // wake up sender
                sys_semaphore_signal(inet_sock->tx_event);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function bind selected address with socket.
 * @param  inet_sock     socket
 * @param  addr          inet address
 * @param  addr_size     address size
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_bind(void *ctx, INET_socket_t *inet_sock, const NETM_INET_sockaddr_t *addr, size_t addr_size)
{
        UNUSED_ARG1(ctx);

        if (addr_size != sizeof(*addr)) {
                return EINVAL;
        }

        if (not sock_op_begin(inet_sock, false)) {
                return ECONNABORTED;
        }

        ip_addr_t IP;
        create_lwIP_addr(&IP, &addr->addr);

        int err = err_to_errno(netconn_bind(inet_sock->netconn, &IP, addr->port));

        sock_op_end(inet_sock, false);

        return err;
}

//==============================================================================
/**
 * @brief  Function listen connection.
 * @param  socket        socket
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_listen(void *ctx, INET_socket_t *inet_sock)
{
        UNUSED_ARG1(ctx);

        if (not sock_op_begin(inet_sock, false)) {
                return ECONNABORTED;
        }

        int err = err_to_errno(netconn_listen(inet_sock->netconn));

        sock_op_end(inet_sock, false);

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
int INET_socket_accept(void *ctx, INET_socket_t *inet_sock, INET_socket_t *new_inet_sock)
{
        UNUSED_ARG1(ctx);

        int err = sliced_receive(inet_sock, NULL, &new_inet_sock->netconn);
        if (!err) {
                err = socket_init(new_inet_sock);
                if (err) {
                        netconn_close(new_inet_sock->netconn);
                        netconn_delete(new_inet_sock->netconn);
                        new_inet_sock->netconn = NULL;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function receive incoming data.
 * @param  inet_sock    socket
 * @param  buf          buffer for data
 * @param  len          number of data to receive
 * @param  flags        flags
 * @param  recved       number of received bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_recv(void *ctx, INET_socket_t *inet_sock, void *buf, size_t len, NETM_flags_t flags, size_t *recved)
{
        UNUSED_ARG1(ctx);

        if (flags & NETM_FLAGS__REWIND) {
                inet_sock->seek = 0;
        }

        int err = ESUCC;
        if (inet_sock->netbuf_recv == NULL) {
                err = sliced_receive(inet_sock, &inet_sock->netbuf_recv, NULL);
        }

        if (!err) {
                len = min(len, UINT16_MAX);

                u16_t sz = netbuf_copy_partial(inet_sock->netbuf_recv, buf, len, inet_sock->seek);

                inet_sock->seek += sz;
                *recved = sz;

                if ((flags & NETM_FLAGS__FREEBUF) || (inet_sock->seek >= netbuf_len(inet_sock->netbuf_recv))) {

                        netbuf_delete(inet_sock->netbuf_recv);
                        inet_sock->netbuf_recv = NULL;
                        inet_sock->seek = 0;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function receive data from selected address.
 * @param  inet_sock            socket
 * @param  buf                  buffer for data
 * @param  len                  number of bytes to receive
 * @param  flags                flags
 * @param  sockaddr             socket address
 * @param  sockaddr_size        socket address size
 * @param  recved               number of received bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_recvfrom(void *ctx, INET_socket_t *inet_sock, void *buf, size_t len, NETM_flags_t flags,
        NETM_INET_sockaddr_t *sockaddr, size_t sockaddr_size, size_t *recved)
{
        UNUSED_ARG2(ctx, flags);

        if (sockaddr_size != sizeof(*sockaddr)) {
                return EINVAL;
        }

        int err = EPERM;

        enum netconn_type type = netconn_type(inet_sock->netconn);

        if ((type == NETCONN_UDP) or (type == NETCONN_RAW)) {
                struct netbuf *netbuf;
                err = sliced_receive(inet_sock, &netbuf, NULL);
                if (!err) {
                        len = min(len, UINT16_MAX);
                        *recved = netbuf_copy(netbuf, buf, len);

                        ip_addr_t *fromaddr = netbuf_fromaddr(netbuf);
                        sockaddr->port = netbuf_fromport(netbuf);

                        create_addr(&sockaddr->addr, fromaddr);

                        netbuf_delete(netbuf);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function send datagram (UDP or RAW socket).
 * @param  inet_sock    socket
 * @param  buf          buffer to send
 * @param  len          number of bytes to send
 * @param  flags        flags
 * @param  addr         destination address (NULL if socket is connected)
 * @param  port         destination port (used if addr is set)
 * @param  sent         number of sent bytes
 * @return One of @ref errno value.
 */
//==============================================================================
static int send_datagram(INET_socket_t *inet_sock, const void *buf, size_t len, NETM_flags_t flags,
        const ip_addr_t *addr, u16_t port, size_t *sent)
{
        if (len > MAXIMUM_SAFE_UDP_PAYLOAD) {
                return EFBIG;
        }

        if (not inet_sock->netbuf_send) {
                inet_sock->netbuf_send = netbuf_new();
                if (not inet_sock->netbuf_send) {
                        return ENOMEM;
                }
        }

        int err = ESUCC;

        if ((flags & NETM_FLAGS__NOCOPY) && (netconn_type(inet_sock->netconn) == NETCONN_UDP)) {
                err = err_to_errno(netbuf_ref(inet_sock->netbuf_send, buf, len));
        } else {
                char *data = netbuf_alloc(inet_sock->netbuf_send, len);
                if (data) {
                        memcpy(data, buf, len);
                } else {
                        err = ENOMEM;
                }
        }

        if (!err) {
                if (sock_op_begin(inet_sock, false)) {
                        if (addr) {
                                err = err_to_errno(netconn_sendto(inet_sock->netconn, inet_sock->netbuf_send,
                                        const_cast(addr), port));
                        } else {
                                /*
                                 * Netbuf is reused, so destination address set by previous
                                 * sendto() must be cleared. Otherwise lwIP sends datagram to
                                 * that address instead of connected one.
                                 */
                                ip_addr_set_zero(&inet_sock->netbuf_send->addr);
                                inet_sock->netbuf_send->port = 0;

                                err = err_to_errno(netconn_send(inet_sock->netconn, inet_sock->netbuf_send));
                        }

                        sock_op_end(inet_sock, false);
                } else {
                        err = ECONNABORTED;
                }
        }

        // the packet is already sent (or copied by stack), so data reference is not needed
        netbuf_free(inet_sock->netbuf_send);

        if (!err) {
                *sent = len;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function sends data by TCP connection. Data is written without
 *         blocking in lwIP; when send buffer is full the function waits for
 *         space (signaled by netconn event) in slices. Between slices it
 *         checks if socket is not closing and if process is not killed.
 * @param  inet_sock    socket
 * @param  buf          buffer to send
 * @param  len          number of bytes to send
 * @param  flags        flags
 * @param  sent         number of sent bytes
 * @return One of @ref errno value. If part of data was sent then ESUCC is
 *         returned with number of sent bytes.
 */
//==============================================================================
static int send_stream(INET_socket_t *inet_sock, const void *buf, size_t len, NETM_flags_t flags, size_t *sent)
{
        u8_t lwip_flags = NETCONN_DONTBLOCK;

        if (flags & NETM_FLAGS__NOCOPY) {
                lwip_flags |= NETCONN_NOCOPY;
        } else {
                lwip_flags |= NETCONN_COPY;
        }

        if (flags & NETM_FLAGS__MORE) {
                lwip_flags |= NETCONN_MORE;
        }

        const u8_t *data = buf;
        clock_t tref = sys_get_uptime_ms();
        u32_t timeout = inet_sock->send_timeout;
        int err = ESUCC;

        *sent = 0;

        while (*sent < len) {
                err = sock_abort_check(inet_sock, false);
                if (err) {
                        break;
                }

                if (not sock_op_begin(inet_sock, false)) {
                        err = ECONNABORTED;
                        break;
                }

                size_t written = 0;
                err_t lwip_err = netconn_write_partly(inet_sock->netconn, &data[*sent], len - *sent, lwip_flags,
                        &written);

                sock_op_end(inet_sock, false);

                *sent += written;

                if ((lwip_err != ERR_OK) and (lwip_err != ERR_WOULDBLOCK)) {
                        err = err_to_errno(lwip_err);
                        break;
                }

                if (*sent < len) {
                        u32_t slice;
                        if (not next_slice(tref, timeout, &slice)) {
                                err = err_to_errno(ERR_WOULDBLOCK);
                                break;
                        }

                        // event is signaled when space is available (or connection error)
                        sys_semaphore_wait(inet_sock->tx_event, max(slice, 1));
                }
        }

        if (err and (*sent > 0)) {
                err = ESUCC;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function send data to connected socket.
 * @param  inet_sock    socket
 * @param  buf          buffer to send
 * @param  len          number of bytes to send
 * @param  flags        flags
 * @param  sent         number of sent bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_send(void *ctx, INET_socket_t *inet_sock, const void *buf, size_t len, NETM_flags_t flags, size_t *sent)
{
        UNUSED_ARG1(ctx);

        int err = EINVAL;

        *sent = 0;

        enum netconn_type type = netconn_type(inet_sock->netconn);

        if (type == NETCONN_TCP) {
                err = send_stream(inet_sock, buf, len, flags, sent);

        } else if ((type == NETCONN_UDP) || (type == NETCONN_RAW)) {
                err = send_datagram(inet_sock, buf, len, flags, NULL, 0, sent);

        } else {
                err = EFAULT;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function send buffer to selected address.
 * @param  inet_sock            socket
 * @param  buf                  buffer to send
 * @param  len                  number of bytes to send
 * @param  flags                flags
 * @param  to_sockaddr          socket address
 * @param  to_sockaddr_size     socket address size
 * @param  sent                 number of sent bytes
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_sendto(void *ctx, INET_socket_t *inet_sock, const void *buf, size_t len, NETM_flags_t flags,
        const NETM_INET_sockaddr_t *to_sockaddr, size_t to_sockaddr_size, size_t *sent)
{
        UNUSED_ARG1(ctx);

        if (to_sockaddr_size != sizeof(*to_sockaddr)) {
                return EINVAL;
        }

        int err = EPERM;

        *sent = 0;

        enum netconn_type type = netconn_type(inet_sock->netconn);

        if ((type == NETCONN_UDP) || (type == NETCONN_RAW)) {
                ip_addr_t lwip_addr;
                create_lwIP_addr(&lwip_addr, &to_sockaddr->addr);

                err = send_datagram(inet_sock, buf, len, flags, &lwip_addr, to_sockaddr->port, sent);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function gets host address by name.
 * @param  name                 address name
 * @param  addr                 received address
 * @param  sock_addr_size       recieved address size (input)
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_gethostbyname(void *ctx, const char *name, NETM_INET_sockaddr_t *sock_addr, size_t sock_addr_size)
{
        UNUSED_ARG1(ctx);

        int err = EINVAL;

        if (sock_addr_size != sizeof(*sock_addr)) {
                return err;
        }

        if (isstreq(name, "localhost") or isstreq(name, __OS_HOSTNAME__)) {
                sock_addr->addr = NETM_INET_IPv4_LOOPBACK;
                err = 0;

        } else {
                ip_addr_t lwip_addr;
                err = err_to_errno(netconn_gethostbyname(name, &lwip_addr));
                if (!err) {
                        create_addr(&sock_addr->addr, &lwip_addr);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function set receive timeout.
 * @param  inet_sock    socket
 * @param  timeout      timeout value in milliseconds (0: no wait,
 *                      _WAIT_FOREVER_MS: infinite)
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_set_recv_timeout(void *ctx, INET_socket_t *inet_sock, uint32_t timeout)
{
        UNUSED_ARG1(ctx);

        inet_sock->recv_timeout = timeout;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function set send timeout.
 * @param  inet_sock    socket
 * @param  timeout      timeout value in milliseconds (0: no wait,
 *                      _WAIT_FOREVER_MS: infinite)
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_set_send_timeout(void *ctx, INET_socket_t *inet_sock, uint32_t timeout)
{
        UNUSED_ARG1(ctx);

        inet_sock->send_timeout = timeout;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function get receive timeout.
 * @param  inet_sock    socket
 * @param  timeout      timeout value in milliseconds
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_get_recv_timeout(void *ctx, INET_socket_t *inet_sock, uint32_t *timeout)
{
        UNUSED_ARG1(ctx);

        *timeout = inet_sock->recv_timeout;
        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function get send timeout.
 * @param  inet_sock    socket
 * @param  timeout      timeout value in milliseconds
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_get_send_timeout(void *ctx, INET_socket_t *inet_sock, uint32_t *timeout)
{
        UNUSED_ARG1(ctx);

        *timeout = inet_sock->send_timeout;
        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function returns address of socket (remote connection address).
 * @param  inet_sock            socket
 * @param  sockaddr             socket address (address and port)
 * @param  sockaddr_size        socket address size
 * @return One of @ref errno value.
 */
//==============================================================================
int INET_socket_getaddress(void *ctx, INET_socket_t *inet_sock, NETM_INET_sockaddr_t *sockaddr, size_t sockaddr_size)
{
        UNUSED_ARG1(ctx);

        if (sockaddr_size != sizeof(*sockaddr)) {
                return EINVAL;
        }

        if (not sock_op_begin(inet_sock, false)) {
                return ECONNABORTED;
        }

        ip_addr_t lwip_addr;
        int err = err_to_errno(netconn_getaddr(inet_sock->netconn, &lwip_addr, &sockaddr->port, 0));

        sock_op_end(inet_sock, false);

        if (!err) {
                create_addr(&sockaddr->addr, &lwip_addr);
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
u16_t INET_hton_u16(u16_t value)
{
#if _CPUCTL_BYTE_ORDER == _BYTE_ORDER_LITTLE_ENDIAN
        return ((value & 0x00FF) << 8) | ((value & 0xFF00) >> 8);
#elif _CPUCTL_BYTE_ORDER == _BYTE_ORDER_BIG_ENDIAN
        return value;
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
u32_t INET_hton_u32(u32_t value)
{
#if _CPUCTL_BYTE_ORDER == _BYTE_ORDER_LITTLE_ENDIAN
        return ((value & 0x000000FFUL) << 24) | ((value & 0x0000FF00UL) << 8) | ((value & 0x00FF0000UL) >> 8) |
                ((value & 0xFF000000UL) >> 24);
#elif _CPUCTL_BYTE_ORDER == _BYTE_ORDER_BIG_ENDIAN
        return value;
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
u64_t INET_hton_u64(u64_t value)
{
#if _CPUCTL_BYTE_ORDER == _BYTE_ORDER_LITTLE_ENDIAN
        return ((value & 0x00000000000000FFULL) << 56) | ((value & 0x000000000000FF00ULL) << 40) |
                ((value & 0x0000000000FF0000ULL) << 24) | ((value & 0x00000000FF000000ULL) << 8) |
                ((value & 0x000000FF00000000ULL) >> 8) | ((value & 0x0000FF0000000000ULL) >> 24) |
                ((value & 0x00FF000000000000ULL) >> 40) | ((value & 0xFF00000000000000ULL) >> 56);
#elif _CPUCTL_BYTE_ORDER == _BYTE_ORDER_BIG_ENDIAN
        return value;
#else
#error "Not supported endianness!"
#endif
}

/*==============================================================================
  End of file
==============================================================================*/
