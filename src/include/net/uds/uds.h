/*=============================================================================
File     uds.h

Author   Daniel Zorychta

Brief    Unix Domain Socket communication.

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

/**
@defgroup UDS_H_ UDS_H_

Detailed Doxygen description.
*/
/**@{*/

#ifndef _UDS_H_
#define _UDS_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "lib/sys/types.h"
#include "net/netm.h"
#include "kernel/sysfunc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/

/*==============================================================================
  Exported object types
==============================================================================*/
typedef struct UDS_socket {
        char              *path;
        kqueue_t          *buf;
        kqueue_t          *conn_req;
        struct UDS_socket *conn_sock;
        NETM_protocol_t    protocol;
        u32_t              recv_timeout;
        u32_t              send_timeout;
        NETM_shut_t        shutdown;
        bool               connected;
} UDS_socket_t;

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/
extern int   UDS_ifinit(void **, const char *);
extern int   UDS_ifdeinit(void *);
extern int   UDS_ifup(void *, const NETM_UDS_config_t *, size_t);
extern int   UDS_ifdown(void *);
extern int   UDS_ifstatus(void *, NETM_UDS_status_t *, size_t);
extern int   UDS_socket_create(void *, NETM_protocol_t, UDS_socket_t *);
extern int   UDS_socket_destroy(void *, UDS_socket_t *);
extern int   UDS_socket_connect(void *, UDS_socket_t *, const NETM_UDS_sockaddr_t *,
          size_t);
extern int   UDS_socket_disconnect(void *, UDS_socket_t *);
extern int   UDS_socket_shutdown(void *, UDS_socket_t *, NETM_shut_t);
extern int   UDS_socket_bind(void *, UDS_socket_t *, const NETM_UDS_sockaddr_t *, size_t);
extern int   UDS_socket_listen(void *, UDS_socket_t *);
extern int   UDS_socket_accept(void *, UDS_socket_t *, UDS_socket_t *);
extern int   UDS_socket_recv(void *, UDS_socket_t *, void *, size_t, NETM_flags_t,
          size_t *);
extern int   UDS_socket_recvfrom(void *, UDS_socket_t *, void *, size_t, NETM_flags_t,
          NETM_UDS_sockaddr_t *, size_t, size_t *);
extern int   UDS_socket_send(void *, UDS_socket_t *, const void *, size_t, NETM_flags_t,
          size_t *);
extern int   UDS_socket_sendto(void *, UDS_socket_t *, const void *, size_t, NETM_flags_t,
          const NETM_UDS_sockaddr_t *, size_t, size_t *);
extern int   UDS_gethostbyname(void *, const char *, NETM_UDS_sockaddr_t *, size_t);
extern int   UDS_socket_set_recv_timeout(void *, UDS_socket_t *, uint32_t);
extern int   UDS_socket_set_send_timeout(void *, UDS_socket_t *, uint32_t);
extern int   UDS_socket_get_recv_timeout(void *, UDS_socket_t *, uint32_t *);
extern int   UDS_socket_get_send_timeout(void *, UDS_socket_t *, uint32_t *);
extern int   UDS_socket_getaddress(void *, UDS_socket_t *, NETM_UDS_sockaddr_t *, size_t);
extern u16_t UDS_hton_u16(u16_t);
extern u32_t UDS_hton_u32(u32_t);
extern u64_t UDS_hton_u64(u64_t);

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _UDS_H_ */

/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
