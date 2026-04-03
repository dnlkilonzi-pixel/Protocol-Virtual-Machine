/**
 * pvm_socket.h — Network Kernel Mode: POSIX-like Socket API
 *
 * Provides a familiar socket interface on top of the PVM runtime,
 * making the PVM a drop-in replacement for OS sockets.  Applications
 * use pvm_socket/bind/listen/accept/connect/send/recv/close exactly
 * like they would use BSD sockets — but all traffic is routed through
 * the PVM protocol engine, scheduler, pipeline, and PAL.
 *
 * This turns the PVM into a user-space networking kernel.
 *
 * Example:
 *   int fd = pvm_socket(PVM_SOCK_STREAM, "vesper_lite");
 *   pvm_sock_connect(fd, "192.168.1.10", 8080);
 *   pvm_sock_send(fd, data, len, 0);
 *   int n = pvm_sock_recv(fd, buf, sizeof(buf), 0);
 *   pvm_sock_close(fd);
 */
#ifndef PVM_SOCKET_H
#define PVM_SOCKET_H

#include <stdint.h>
#include <stddef.h>

/** Maximum number of concurrent PVM sockets. */
#define PVM_MAX_SOCKETS   64

/** Socket types (analogous to SOCK_STREAM / SOCK_DGRAM). */
typedef enum {
    PVM_SOCK_DGRAM   = 0,  /**< Connectionless datagram (UDP-like).       */
    PVM_SOCK_STREAM  = 1,  /**< Connection-oriented stream (TCP-like).    */
    PVM_SOCK_RAW     = 2,  /**< Raw PVM frames (no protocol framing).     */
} PvmSockType;

/** Socket states. */
typedef enum {
    PVM_SOCK_STATE_CLOSED     = 0,
    PVM_SOCK_STATE_CREATED    = 1,
    PVM_SOCK_STATE_BOUND      = 2,
    PVM_SOCK_STATE_LISTENING  = 3,
    PVM_SOCK_STATE_CONNECTED  = 4,
} PvmSockState;

/** Socket address structure. */
typedef struct {
    char        addr[64];       /**< IP address or hostname.               */
    uint16_t    port;           /**< Port number.                          */
} PvmSockAddr;

/** Socket descriptor (returned to user). */
typedef struct {
    int           fd;            /**< File descriptor number.              */
    PvmSockType   type;          /**< Socket type.                         */
    PvmSockState  state;         /**< Current state.                       */
    char          protocol[64];  /**< Protocol module name.                */
    PvmSockAddr   local_addr;    /**< Local bind address.                  */
    PvmSockAddr   remote_addr;   /**< Remote peer address.                 */
    uint64_t      bytes_sent;    /**< Total bytes sent.                    */
    uint64_t      bytes_recv;    /**< Total bytes received.                */
    int           active;        /**< 1 = in use, 0 = available.           */
} PvmSocket;

/* =========================================================================
 * Public API — mirrors BSD socket interface
 * ====================================================================== */

/**
 * pvm_socket — Create a PVM socket.
 *
 * @param type      Socket type (PVM_SOCK_DGRAM, PVM_SOCK_STREAM, PVM_SOCK_RAW).
 * @param protocol  Protocol module name to use (e.g. "udp", "vesper_lite").
 *                  If NULL, the scheduler picks the best protocol.
 * @return          Socket file descriptor (>= 0) on success, -1 on error.
 */
int pvm_socket(PvmSockType type, const char *protocol);

/**
 * pvm_sock_bind — Bind a socket to a local address and port.
 *
 * @param fd    Socket file descriptor.
 * @param addr  Local address string (or NULL / "" for any).
 * @param port  Local port number.
 * @return      0 on success, -1 on error.
 */
int pvm_sock_bind(int fd, const char *addr, uint16_t port);

/**
 * pvm_sock_listen — Mark a socket as listening for incoming connections.
 *
 * @param fd       Socket file descriptor.
 * @param backlog  Maximum pending connections (informational; not enforced).
 * @return         0 on success, -1 on error.
 */
int pvm_sock_listen(int fd, int backlog);

/**
 * pvm_sock_accept — Accept an incoming connection (stub for future use).
 *
 * @param fd      Listening socket file descriptor.
 * @param remote  Filled with the remote peer's address.
 * @return        New socket fd on success, -1 if none pending.
 */
int pvm_sock_accept(int fd, PvmSockAddr *remote);

/**
 * pvm_sock_connect — Connect a socket to a remote endpoint.
 *
 * Switches the PVM to the socket's protocol, connects, and marks
 * the socket as CONNECTED.
 *
 * @param fd    Socket file descriptor.
 * @param addr  Remote address string.
 * @param port  Remote port number.
 * @return      0 on success, -1 on error.
 */
int pvm_sock_connect(int fd, const char *addr, uint16_t port);

/**
 * pvm_sock_send — Send data through a connected socket.
 *
 * @param fd     Socket file descriptor.
 * @param data   Payload buffer.
 * @param len    Payload length.
 * @param flags  Reserved (pass 0).
 * @return       Bytes sent, or -1 on error.
 */
int pvm_sock_send(int fd, const uint8_t *data, size_t len, int flags);

/**
 * pvm_sock_recv — Receive data from a connected socket.
 *
 * @param fd       Socket file descriptor.
 * @param buffer   Destination buffer.
 * @param max_len  Buffer capacity.
 * @param flags    Reserved (pass 0).
 * @return         Bytes received, or -1 on error / no data.
 */
int pvm_sock_recv(int fd, uint8_t *buffer, size_t max_len, int flags);

/**
 * pvm_sock_close — Close a PVM socket and release resources.
 *
 * @param fd  Socket file descriptor.
 * @return    0 on success, -1 on error.
 */
int pvm_sock_close(int fd);

/**
 * pvm_sock_info — Get information about a socket.
 *
 * @param fd   Socket file descriptor.
 * @return     Pointer to the socket descriptor, or NULL if invalid.
 */
const PvmSocket *pvm_sock_info(int fd);

/**
 * pvm_sock_list — Print all active sockets to stdout.
 */
void pvm_sock_list(void);

/**
 * pvm_sock_type_name — Human-readable socket type name.
 */
const char *pvm_sock_type_name(PvmSockType t);

/**
 * pvm_sock_state_name — Human-readable socket state name.
 */
const char *pvm_sock_state_name(PvmSockState s);

#endif /* PVM_SOCKET_H */
