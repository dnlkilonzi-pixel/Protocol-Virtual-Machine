/**
 * core/pvm_socket.c - Network Kernel Mode: POSIX-like Socket API
 *
 * Implements a familiar BSD socket interface on top of the PVM runtime.
 * Applications call pvm_socket/bind/connect/send/recv/close exactly
 * like OS sockets, but all traffic flows through the PVM engine.
 *
 * No OS-specific code - all I/O goes through pvm.h.
 */

#include <stdio.h>
#include <string.h>

#include "pvm_socket.h"
#include "pvm.h"

/* -------------------------------------------------------------------------
 * Socket table
 * ---------------------------------------------------------------------- */

static PvmSocket socket_table[PVM_MAX_SOCKETS];
static int       sock_inited = 0;
static int       next_fd     = 0;

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static PvmSocket *get_socket(int fd)
{
    if (fd < 0 || fd >= PVM_MAX_SOCKETS) return NULL;
    if (!socket_table[fd].active) return NULL;
    return &socket_table[fd];
}

static void init_table_once(void)
{
    if (!sock_inited) {
        memset(socket_table, 0, sizeof(socket_table));
        next_fd = 0;
        sock_inited = 1;
    }
}

/* =========================================================================
 * Public API
 * ====================================================================== */

int pvm_socket(PvmSockType type, const char *protocol)
{
    init_table_once();

    /* Find a free slot. */
    int fd = -1;
    for (int i = 0; i < PVM_MAX_SOCKETS; ++i) {
        int idx = (next_fd + i) % PVM_MAX_SOCKETS;
        if (!socket_table[idx].active) {
            fd = idx;
            break;
        }
    }
    if (fd < 0) {
        fprintf(stderr, "[PVM-Socket] No free sockets available.\n");
        return -1;
    }

    PvmSocket *s = &socket_table[fd];
    memset(s, 0, sizeof(*s));
    s->fd     = fd;
    s->type   = type;
    s->state  = PVM_SOCK_STATE_CREATED;
    s->active = 1;

    if (protocol && *protocol != '\0') {
        strncpy(s->protocol, protocol, sizeof(s->protocol) - 1);
        s->protocol[sizeof(s->protocol) - 1] = '\0';
    } else {
        strncpy(s->protocol, "udp", sizeof(s->protocol) - 1);
    }

    next_fd = (fd + 1) % PVM_MAX_SOCKETS;

    printf("[PVM-Socket] Created socket fd=%d type=%s protocol='%s'\n",
           fd, pvm_sock_type_name(type), s->protocol);
    return fd;
}

int pvm_sock_bind(int fd, const char *addr, uint16_t port)
{
    PvmSocket *s = get_socket(fd);
    if (!s) {
        fprintf(stderr, "[PVM-Socket] bind: invalid fd=%d\n", fd);
        return -1;
    }
    if (s->state != PVM_SOCK_STATE_CREATED) {
        fprintf(stderr, "[PVM-Socket] bind: socket not in CREATED state.\n");
        return -1;
    }

    if (addr && *addr != '\0') {
        strncpy(s->local_addr.addr, addr, sizeof(s->local_addr.addr) - 1);
        s->local_addr.addr[sizeof(s->local_addr.addr) - 1] = '\0';
    } else {
        strncpy(s->local_addr.addr, "0.0.0.0", sizeof(s->local_addr.addr) - 1);
    }
    s->local_addr.port = port;
    s->state = PVM_SOCK_STATE_BOUND;

    printf("[PVM-Socket] fd=%d bound to %s:%u\n", fd, s->local_addr.addr, port);
    return 0;
}

int pvm_sock_listen(int fd, int backlog)
{
    PvmSocket *s = get_socket(fd);
    if (!s) return -1;
    if (s->state != PVM_SOCK_STATE_BOUND) {
        fprintf(stderr, "[PVM-Socket] listen: socket not bound.\n");
        return -1;
    }

    s->state = PVM_SOCK_STATE_LISTENING;
    printf("[PVM-Socket] fd=%d listening (backlog=%d)\n", fd, backlog);
    return 0;
}

int pvm_sock_accept(int fd, PvmSockAddr *remote)
{
    PvmSocket *s = get_socket(fd);
    if (!s) return -1;
    if (s->state != PVM_SOCK_STATE_LISTENING) {
        fprintf(stderr, "[PVM-Socket] accept: socket not listening.\n");
        return -1;
    }

    /* In a full implementation, this would block until a connection
     * request arrives.  For this demo, we create a new connected socket
     * representing an accepted connection. */
    int new_fd = pvm_socket(s->type, s->protocol);
    if (new_fd < 0) return -1;

    PvmSocket *ns = get_socket(new_fd);
    if (!ns) return -1;

    ns->state = PVM_SOCK_STATE_CONNECTED;
    if (remote) {
        strncpy(ns->remote_addr.addr, remote->addr, sizeof(ns->remote_addr.addr) - 1);
        ns->remote_addr.addr[sizeof(ns->remote_addr.addr) - 1] = '\0';
        ns->remote_addr.port = remote->port;
    }

    printf("[PVM-Socket] fd=%d accepted connection -> new fd=%d\n", fd, new_fd);
    return new_fd;
}

int pvm_sock_connect(int fd, const char *addr, uint16_t port)
{
    PvmSocket *s = get_socket(fd);
    if (!s) {
        fprintf(stderr, "[PVM-Socket] connect: invalid fd=%d\n", fd);
        return -1;
    }

    /* Switch PVM to this socket's protocol. */
    if (pvm_switch(s->protocol) != 0) {
        fprintf(stderr, "[PVM-Socket] connect: failed to switch to '%s'\n", s->protocol);
        return -1;
    }

    /* Connect through the PVM. */
    if (pvm_connect(addr, port) != 0) {
        fprintf(stderr, "[PVM-Socket] connect: pvm_connect failed.\n");
        return -1;
    }

    strncpy(s->remote_addr.addr, addr, sizeof(s->remote_addr.addr) - 1);
    s->remote_addr.addr[sizeof(s->remote_addr.addr) - 1] = '\0';
    s->remote_addr.port = port;
    s->state = PVM_SOCK_STATE_CONNECTED;

    printf("[PVM-Socket] fd=%d connected to %s:%u via '%s'\n",
           fd, addr, port, s->protocol);
    return 0;
}

int pvm_sock_send(int fd, const uint8_t *data, size_t len, int flags)
{
    (void)flags;
    PvmSocket *s = get_socket(fd);
    if (!s) return -1;
    if (s->state != PVM_SOCK_STATE_CONNECTED) {
        fprintf(stderr, "[PVM-Socket] send: socket not connected.\n");
        return -1;
    }

    /* Ensure we're on the right protocol. */
    pvm_switch(s->protocol);

    int sent = pvm_send(data, len);
    if (sent > 0) s->bytes_sent += (uint64_t)sent;
    return sent;
}

int pvm_sock_recv(int fd, uint8_t *buffer, size_t max_len, int flags)
{
    (void)flags;
    PvmSocket *s = get_socket(fd);
    if (!s) return -1;
    if (s->state != PVM_SOCK_STATE_CONNECTED) {
        fprintf(stderr, "[PVM-Socket] recv: socket not connected.\n");
        return -1;
    }

    /* Ensure we're on the right protocol. */
    pvm_switch(s->protocol);

    int n = pvm_receive(buffer, max_len);
    if (n > 0) s->bytes_recv += (uint64_t)n;
    return n;
}

int pvm_sock_close(int fd)
{
    PvmSocket *s = get_socket(fd);
    if (!s) return -1;

    printf("[PVM-Socket] fd=%d closed (sent=%lu recv=%lu)\n",
           fd, (unsigned long)s->bytes_sent, (unsigned long)s->bytes_recv);

    memset(s, 0, sizeof(*s));
    return 0;
}

const PvmSocket *pvm_sock_info(int fd)
{
    return get_socket(fd);
}

void pvm_sock_list(void)
{
    init_table_once();
    int count = 0;
    printf("[PVM-Socket] Active sockets:\n");
    for (int i = 0; i < PVM_MAX_SOCKETS; ++i) {
        if (socket_table[i].active) {
            PvmSocket *s = &socket_table[i];
            printf("  fd=%-3d  %-8s  %-11s  proto=%-14s  ",
                   s->fd,
                   pvm_sock_type_name(s->type),
                   pvm_sock_state_name(s->state),
                   s->protocol);
            if (s->state >= PVM_SOCK_STATE_CONNECTED) {
                printf("%s:%u", s->remote_addr.addr, s->remote_addr.port);
            } else if (s->state >= PVM_SOCK_STATE_BOUND) {
                printf("[bound %s:%u]", s->local_addr.addr, s->local_addr.port);
            }
            printf("  (sent=%lu recv=%lu)\n",
                   (unsigned long)s->bytes_sent, (unsigned long)s->bytes_recv);
            ++count;
        }
    }
    if (count == 0)
        printf("  (none)\n");
}

const char *pvm_sock_type_name(PvmSockType t)
{
    switch (t) {
        case PVM_SOCK_DGRAM:  return "DGRAM";
        case PVM_SOCK_STREAM: return "STREAM";
        case PVM_SOCK_RAW:    return "RAW";
        default:              return "???";
    }
}

const char *pvm_sock_state_name(PvmSockState s)
{
    switch (s) {
        case PVM_SOCK_STATE_CLOSED:    return "CLOSED";
        case PVM_SOCK_STATE_CREATED:   return "CREATED";
        case PVM_SOCK_STATE_BOUND:     return "BOUND";
        case PVM_SOCK_STATE_LISTENING: return "LISTENING";
        case PVM_SOCK_STATE_CONNECTED: return "CONNECTED";
        default:                       return "???";
    }
}
