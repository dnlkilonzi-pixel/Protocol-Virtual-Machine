/**
 * mesh.h — Distributed PVM Mesh Network
 *
 * Enables multiple PVM instances (nodes) to form a self-adapting
 * distributed system.  Nodes share:
 *
 *   - Scheduler state (network metrics)
 *   - Protocol module capabilities
 *   - Negotiation results
 *
 * The mesh uses a simple peer registry and gossip-style state exchange
 * over the existing PAL.  Each node is identified by a unique node_id
 * and advertises its capabilities to all known peers.
 *
 * Wire format (proto_id = 0xFD):
 *   [0xFD] [msg_type (1B)] [node_id (16B)] [payload ...]
 *
 * Message types:
 *   MESH_HEARTBEAT  — "I'm alive, here are my metrics"
 *   MESH_CAPS       — "Here are my loaded protocol modules"
 *   MESH_SYNC       — "Here is my scheduler state"
 *   MESH_ELECT      — "I propose protocol X for the mesh"
 *   MESH_AGREE      — "I agree to use protocol X"
 *
 * Example:
 *   pvm_mesh_init("node-A");
 *   pvm_mesh_add_peer("node-B", "192.168.1.2", 9001);
 *   pvm_mesh_broadcast_caps();
 *   pvm_mesh_sync_metrics();
 *   pvm_mesh_elect_protocol(&result);
 */
#ifndef MESH_H
#define MESH_H

#include <stdint.h>
#include <stddef.h>

/** Protocol ID reserved for mesh control messages. */
#define PROTO_MESH      0xFDu

/** Maximum number of mesh peers. */
#define MESH_MAX_PEERS  32

/** Node ID length. */
#define MESH_NODE_ID_LEN  16

/** Maximum protocols a node can advertise. */
#define MESH_MAX_PROTOS   16

/** Protocol name width in wire format. */
#define MESH_PROTO_NAME_WIDTH  32

/* -------------------------------------------------------------------------
 * Mesh message types
 * ---------------------------------------------------------------------- */
typedef enum {
    MESH_MSG_HEARTBEAT = 0x01,  /**< Node alive + metrics.                 */
    MESH_MSG_CAPS      = 0x02,  /**< Protocol capabilities.                */
    MESH_MSG_SYNC      = 0x03,  /**< Scheduler state sync.                 */
    MESH_MSG_ELECT     = 0x04,  /**< Protocol election proposal.           */
    MESH_MSG_AGREE     = 0x05,  /**< Agreement to proposed protocol.       */
} MeshMsgType;

/* -------------------------------------------------------------------------
 * Mesh node state
 * ---------------------------------------------------------------------- */
typedef enum {
    MESH_NODE_UNKNOWN  = 0,
    MESH_NODE_ALIVE    = 1,
    MESH_NODE_DEAD     = 2,
} MeshNodeState;

/* -------------------------------------------------------------------------
 * Peer descriptor
 * ---------------------------------------------------------------------- */
typedef struct {
    char           node_id[MESH_NODE_ID_LEN];  /**< Unique node identifier.  */
    char           addr[64];                    /**< Network address.         */
    uint16_t       port;                        /**< Network port.            */
    MeshNodeState  state;                       /**< Current state.           */
    int32_t        latency_ms;                  /**< Last known latency.      */
    int32_t        loss_pct;                    /**< Last known loss %.       */
    char           protocols[MESH_MAX_PROTOS][MESH_PROTO_NAME_WIDTH];
    int            proto_count;                 /**< Number of protocols.     */
    int            active;                      /**< 1 = slot in use.         */
} MeshPeer;

/* -------------------------------------------------------------------------
 * Mesh election result
 * ---------------------------------------------------------------------- */
typedef struct {
    int  success;                  /**< 1 = consensus reached.             */
    char protocol[64];             /**< Agreed protocol name.              */
    int  supporters;               /**< Number of nodes that agreed.       */
    int  total_nodes;              /**< Total nodes in the mesh.           */
} MeshElectionResult;

/* -------------------------------------------------------------------------
 * Mesh statistics
 * ---------------------------------------------------------------------- */
typedef struct {
    int      total_peers;          /**< Total registered peers.            */
    int      alive_peers;          /**< Peers in ALIVE state.             */
    int      dead_peers;           /**< Peers in DEAD state.              */
    uint64_t messages_sent;        /**< Total mesh messages sent.          */
    uint64_t messages_received;    /**< Total mesh messages received.      */
} MeshStats;

/* =========================================================================
 * Public API
 * ====================================================================== */

/**
 * pvm_mesh_init — Initialise the mesh subsystem for this node.
 *
 * @param node_id  Unique identifier for this node (max 15 chars).
 * @return         0 on success, -1 on failure.
 */
int pvm_mesh_init(const char *node_id);

/**
 * pvm_mesh_shutdown — Release mesh state.
 */
void pvm_mesh_shutdown(void);

/**
 * pvm_mesh_add_peer — Register a remote peer node.
 *
 * @param node_id  Peer's unique identifier.
 * @param addr     Peer's network address.
 * @param port     Peer's network port.
 * @return         Peer index on success, -1 if the table is full.
 */
int pvm_mesh_add_peer(const char *node_id, const char *addr, uint16_t port);

/**
 * pvm_mesh_remove_peer — Remove a peer by node_id.
 */
void pvm_mesh_remove_peer(const char *node_id);

/**
 * pvm_mesh_set_peer_state — Update a peer's network state.
 */
void pvm_mesh_set_peer_state(const char *node_id,
                             MeshNodeState state,
                             int32_t latency_ms,
                             int32_t loss_pct);

/**
 * pvm_mesh_set_peer_protocols — Set a peer's protocol capabilities.
 */
void pvm_mesh_set_peer_protocols(const char *node_id,
                                 const char protocols[][MESH_PROTO_NAME_WIDTH],
                                 int count);

/**
 * pvm_mesh_broadcast_caps — Broadcast this node's capabilities to all peers.
 *
 * Serialises the local loaded modules and "sends" a MESH_MSG_CAPS
 * message.  (In this implementation, the message is built but actual
 * transmission over the PAL is demonstrated, not enforced.)
 *
 * @return  Number of bytes in the broadcast message, or -1 on error.
 */
int pvm_mesh_broadcast_caps(void);

/**
 * pvm_mesh_sync_metrics — Broadcast scheduler metrics to all peers.
 *
 * @return  0 on success, -1 on error.
 */
int pvm_mesh_sync_metrics(void);

/**
 * pvm_mesh_elect_protocol — Run a consensus election for the mesh protocol.
 *
 * Finds the protocol supported by the most nodes and proposes it.
 * If a majority of alive nodes support it, the election succeeds.
 *
 * @param result  Output: election outcome.
 * @return        0 on success, -1 on error.
 */
int pvm_mesh_elect_protocol(MeshElectionResult *result);

/**
 * pvm_mesh_get_stats — Retrieve mesh statistics.
 */
void pvm_mesh_get_stats(MeshStats *out);

/**
 * pvm_mesh_list_peers — Print all registered peers to stdout.
 */
void pvm_mesh_list_peers(void);

/**
 * pvm_mesh_get_node_id — Return this node's identifier.
 */
const char *pvm_mesh_get_node_id(void);

/**
 * pvm_mesh_set_local_protocols — Set this node's protocol capabilities.
 *
 * @param protocols  Array of protocol name strings.
 * @param count      Number of protocols.
 */
void pvm_mesh_set_local_protocols(const char protocols[][MESH_PROTO_NAME_WIDTH],
                                  int count);

#endif /* MESH_H */
