/**
 * net/mesh.c - Distributed PVM Mesh Network
 *
 * Enables multiple PVM nodes to form a self-adapting distributed system
 * by sharing scheduler state, protocol capabilities, and running
 * consensus elections for mesh-wide protocol selection.
 *
 * No OS-specific code - all I/O goes through the PAL via platform.h.
 */

#include <stdio.h>
#include <string.h>

#include "mesh.h"
#include "pvm.h"
#include "platform.h"
#include "scheduler.h"

/* -------------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------- */

static char       local_node_id[MESH_NODE_ID_LEN];
static MeshPeer   peers[MESH_MAX_PEERS];
static int        peer_count    = 0;
static int        mesh_inited   = 0;
static MeshStats  stats;

/* Local capabilities (populated from PVM module registry). */
static char local_protos[MESH_MAX_PROTOS][MESH_PROTO_NAME_WIDTH];
static int  local_proto_count = 0;

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static MeshPeer *find_peer(const char *node_id)
{
    for (int i = 0; i < peer_count; ++i) {
        if (peers[i].active && strcmp(peers[i].node_id, node_id) == 0)
            return &peers[i];
    }
    return NULL;
}

/* =========================================================================
 * Public API
 * ====================================================================== */

int pvm_mesh_init(const char *node_id)
{
    if (!node_id || *node_id == '\0') return -1;

    memset(local_node_id, 0, sizeof(local_node_id));
    strncpy(local_node_id, node_id, MESH_NODE_ID_LEN - 1);
    local_node_id[MESH_NODE_ID_LEN - 1] = '\0';

    memset(peers, 0, sizeof(peers));
    peer_count = 0;
    memset(&stats, 0, sizeof(stats));
    local_proto_count = 0;
    mesh_inited = 1;

    printf("[Mesh] Node '%s' initialised.\n", local_node_id);
    return 0;
}

void pvm_mesh_shutdown(void)
{
    if (!mesh_inited) return;
    printf("[Mesh] Node '%s' shutting down. Stats: peers=%d sent=%lu recv=%lu\n",
           local_node_id,
           stats.total_peers,
           (unsigned long)stats.messages_sent,
           (unsigned long)stats.messages_received);
    mesh_inited = 0;
}

int pvm_mesh_add_peer(const char *node_id, const char *addr, uint16_t port)
{
    if (!mesh_inited || !node_id) return -1;

    /* Check for duplicate. */
    if (find_peer(node_id)) {
        printf("[Mesh] Peer '%s' already registered.\n", node_id);
        return 0;
    }

    /* Find a free slot. */
    int slot = -1;
    for (int i = 0; i < MESH_MAX_PEERS; ++i) {
        if (!peers[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        fprintf(stderr, "[Mesh] Peer table full.\n");
        return -1;
    }

    MeshPeer *p = &peers[slot];
    memset(p, 0, sizeof(*p));
    strncpy(p->node_id, node_id, MESH_NODE_ID_LEN - 1);
    p->node_id[MESH_NODE_ID_LEN - 1] = '\0';
    if (addr) {
        strncpy(p->addr, addr, sizeof(p->addr) - 1);
        p->addr[sizeof(p->addr) - 1] = '\0';
    }
    p->port   = port;
    p->state  = MESH_NODE_ALIVE;
    p->active = 1;

    if (slot >= peer_count) peer_count = slot + 1;
    stats.total_peers++;

    printf("[Mesh] Peer '%s' added at %s:%u\n", node_id,
           addr ? addr : "?", port);
    return slot;
}

void pvm_mesh_remove_peer(const char *node_id)
{
    MeshPeer *p = find_peer(node_id);
    if (!p) return;
    printf("[Mesh] Peer '%s' removed.\n", node_id);
    p->active = 0;
}

void pvm_mesh_set_peer_state(const char *node_id,
                             MeshNodeState state,
                             int32_t latency_ms,
                             int32_t loss_pct)
{
    MeshPeer *p = find_peer(node_id);
    if (!p) return;
    p->state      = state;
    p->latency_ms = latency_ms;
    p->loss_pct   = loss_pct;
}

void pvm_mesh_set_peer_protocols(const char *node_id,
                                 const char protocols[][MESH_PROTO_NAME_WIDTH],
                                 int count)
{
    MeshPeer *p = find_peer(node_id);
    if (!p) return;
    p->proto_count = (count > MESH_MAX_PROTOS) ? MESH_MAX_PROTOS : count;
    for (int i = 0; i < p->proto_count; ++i) {
        strncpy(p->protocols[i], protocols[i], MESH_PROTO_NAME_WIDTH - 1);
        p->protocols[i][MESH_PROTO_NAME_WIDTH - 1] = '\0';
    }
    printf("[Mesh] Peer '%s' has %d protocols.\n", node_id, p->proto_count);
}

int pvm_mesh_broadcast_caps(void)
{
    if (!mesh_inited) return -1;

    /* Build a MESH_MSG_CAPS message:
     *   [0xFD] [MESH_MSG_CAPS] [node_id(16B)] [count(1B)] [names...] */
    uint8_t buf[2048];
    size_t pos = 0;

    buf[pos++] = PROTO_MESH;
    buf[pos++] = MESH_MSG_CAPS;

    memset(buf + pos, 0, MESH_NODE_ID_LEN);
    memcpy(buf + pos, local_node_id, strlen(local_node_id));
    pos += MESH_NODE_ID_LEN;

    buf[pos++] = (uint8_t)local_proto_count;

    for (int i = 0; i < local_proto_count; ++i) {
        if (pos + MESH_PROTO_NAME_WIDTH > sizeof(buf)) break;
        memset(buf + pos, 0, MESH_PROTO_NAME_WIDTH);
        memcpy(buf + pos, local_protos[i], strlen(local_protos[i]));
        pos += MESH_PROTO_NAME_WIDTH;
    }

    stats.messages_sent++;

    printf("[Mesh] Broadcasting capabilities: %d protocols from node '%s' (%zu bytes)\n",
           local_proto_count, local_node_id, pos);
    return (int)pos;
}

int pvm_mesh_sync_metrics(void)
{
    if (!mesh_inited) return -1;

    /* Build a MESH_MSG_SYNC message with scheduler metrics. */
    printf("[Mesh] Syncing metrics from node '%s':\n", local_node_id);
    printf("  latency_ms = %d\n", pvm_scheduler_get_metric(SCHED_METRIC_LATENCY_MS));
    printf("  loss_pct   = %d\n", pvm_scheduler_get_metric(SCHED_METRIC_LOSS_PCT));
    printf("  bw_kbps    = %d\n", pvm_scheduler_get_metric(SCHED_METRIC_BW_KBPS));
    printf("  jitter_ms  = %d\n", pvm_scheduler_get_metric(SCHED_METRIC_JITTER_MS));

    stats.messages_sent++;
    return 0;
}

int pvm_mesh_elect_protocol(MeshElectionResult *result)
{
    if (!mesh_inited || !result) return -1;
    memset(result, 0, sizeof(*result));

    /* Count how many alive nodes support each protocol.
     * Include local node in the tally. */
    typedef struct { char name[MESH_PROTO_NAME_WIDTH]; int votes; } Tally;
    Tally tally[MESH_MAX_PROTOS * MESH_MAX_PEERS];
    int tally_count = 0;

    /* Helper: add a vote for a protocol. */
    #define ADD_VOTE(proto_name) do { \
        int found = 0; \
        for (int t = 0; t < tally_count; ++t) { \
            if (strcmp(tally[t].name, (proto_name)) == 0) { \
                tally[t].votes++; \
                found = 1; \
                break; \
            } \
        } \
        if (!found && tally_count < (int)(sizeof(tally)/sizeof(tally[0]))) { \
            strncpy(tally[tally_count].name, (proto_name), MESH_PROTO_NAME_WIDTH - 1); \
            tally[tally_count].name[MESH_PROTO_NAME_WIDTH - 1] = '\0'; \
            tally[tally_count].votes = 1; \
            tally_count++; \
        } \
    } while(0)

    /* Local node's protocols. */
    for (int i = 0; i < local_proto_count; ++i)
        ADD_VOTE(local_protos[i]);

    /* Count alive peers. */
    int alive_count = 1; /* include self */
    for (int i = 0; i < peer_count; ++i) {
        if (!peers[i].active || peers[i].state != MESH_NODE_ALIVE) continue;
        alive_count++;
        for (int j = 0; j < peers[i].proto_count; ++j)
            ADD_VOTE(peers[i].protocols[j]);
    }

    #undef ADD_VOTE

    /* Find the protocol with the most votes. */
    int best_idx = -1;
    int best_votes = 0;
    for (int t = 0; t < tally_count; ++t) {
        if (tally[t].votes > best_votes) {
            best_votes = tally[t].votes;
            best_idx = t;
        }
    }

    result->total_nodes = alive_count;

    if (best_idx >= 0 && best_votes > alive_count / 2) {
        result->success = 1;
        result->supporters = best_votes;
        strncpy(result->protocol, tally[best_idx].name,
                sizeof(result->protocol) - 1);
        result->protocol[sizeof(result->protocol) - 1] = '\0';

        printf("[Mesh] Election result: '%s' wins with %d/%d votes (majority)\n",
               result->protocol, best_votes, alive_count);
    } else if (best_idx >= 0) {
        result->success = 0;
        result->supporters = best_votes;
        strncpy(result->protocol, tally[best_idx].name,
                sizeof(result->protocol) - 1);
        result->protocol[sizeof(result->protocol) - 1] = '\0';

        printf("[Mesh] Election result: '%s' has %d/%d votes (no majority)\n",
               result->protocol, best_votes, alive_count);
    } else {
        printf("[Mesh] Election result: no protocols found.\n");
    }

    stats.messages_sent++;
    return 0;
}

void pvm_mesh_get_stats(MeshStats *out)
{
    if (!out) return;
    *out = stats;
    /* Recount alive/dead. */
    out->total_peers = 0;
    out->alive_peers = 0;
    out->dead_peers  = 0;
    for (int i = 0; i < peer_count; ++i) {
        if (peers[i].active) {
            out->total_peers++;
            if (peers[i].state == MESH_NODE_ALIVE) out->alive_peers++;
            else if (peers[i].state == MESH_NODE_DEAD) out->dead_peers++;
        }
    }
}

void pvm_mesh_list_peers(void)
{
    printf("[Mesh] Peers of node '%s' (%d slots):\n", local_node_id, peer_count);
    int active_count = 0;
    for (int i = 0; i < peer_count; ++i) {
        if (!peers[i].active) continue;
        active_count++;
        MeshPeer *p = &peers[i];
        const char *state_str = (p->state == MESH_NODE_ALIVE) ? "ALIVE" :
                                (p->state == MESH_NODE_DEAD)  ? "DEAD"  : "UNKNOWN";
        printf("  [%d] %-15s  %s:%u  state=%s  latency=%dms  loss=%d%%  protos=%d\n",
               i, p->node_id, p->addr, p->port, state_str,
               p->latency_ms, p->loss_pct, p->proto_count);
        for (int j = 0; j < p->proto_count; ++j)
            printf("        - %s\n", p->protocols[j]);
    }
    if (active_count == 0)
        printf("  (no peers)\n");

    /* Also show local capabilities. */
    printf("  Local protocols (%d):\n", local_proto_count);
    for (int i = 0; i < local_proto_count; ++i)
        printf("    - %s\n", local_protos[i]);
    if (local_proto_count == 0)
        printf("    (none)\n");
}

const char *pvm_mesh_get_node_id(void)
{
    return local_node_id;
}

/* -------------------------------------------------------------------------
 * Internal helper: set local capabilities (called from demo)
 * We expose this so the demo can populate local_protos.
 * ---------------------------------------------------------------------- */
void pvm_mesh_set_local_protocols(const char protocols[][MESH_PROTO_NAME_WIDTH],
                                  int count)
{
    local_proto_count = (count > MESH_MAX_PROTOS) ? MESH_MAX_PROTOS : count;
    for (int i = 0; i < local_proto_count; ++i) {
        strncpy(local_protos[i], protocols[i], MESH_PROTO_NAME_WIDTH - 1);
        local_protos[i][MESH_PROTO_NAME_WIDTH - 1] = '\0';
    }
}
