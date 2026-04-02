/**
 * main.c - VESPER OS Protocol Virtual Machine - CLI Demo
 *
 * Demonstrates the full PVM lifecycle plus all five revolutionary features:
 *
 *   ORIGINAL DEMOS (baseline):
 *     1. Runtime initialisation (PAL auto-selected at compile time)
 *     2. Dynamic module loading  (pvm_load)
 *     3. Protocol switching      (pvm_switch)
 *     4. Connecting and sending  (pvm_connect / pvm_send)
 *     5. Receiving own frames    (loopback - same socket, same port)
 *
 *   NEW FEATURE DEMOS:
 *     6. Adaptive Protocol Scheduler  (auto-switch based on metrics)
 *     7. Protocol Composition         (stackable layer pipeline)
 *     8. Protocol Bytecode VM         (protocols as portable scripts)
 *     9. Network Simulation Mode      (chaos testing engine)
 *    10. Cross-Device Negotiation     (capability exchange)
 *
 * Build:
 *   make          - auto-detects OS, builds modules and binary
 *
 * Run:
 *   make test     - builds and runs this demo
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "pvm.h"
#include "packet.h"
#include "scheduler.h"
#include "pipeline.h"
#include "bytecode.h"
#include "simulator.h"
#include "negotiation.h"

/* -------------------------------------------------------------------------
 * Utility - pretty-print a byte buffer
 * ---------------------------------------------------------------------- */
static void dump_hex(const char *label, const uint8_t *buf, size_t len)
{
    printf("  %-22s [%zu bytes]: ", label, len);
    for (size_t i = 0; i < len && i < 32; ++i)
        printf("%02X ", buf[i]);
    if (len > 32) printf("...");
    printf("\n");
}

/* -------------------------------------------------------------------------
 * Demo sections
 * ---------------------------------------------------------------------- */

static void demo_separator(const char *title)
{
    printf("\n");
    for (int i = 0; i < 64; ++i) printf("=");
    printf("\n  %s\n", title);
    for (int i = 0; i < 64; ++i) printf("=");
    printf("\n");
}

/* --- Demo 1: UDP module ------------------------------------------------- */
static void demo_udp(void)
{
    demo_separator("DEMO 1 - UDP Protocol Module");

    if (pvm_switch("udp") != 0) {
        fprintf(stderr, "  [FAIL] Could not switch to UDP module.\n");
        return;
    }

    pvm_connect("127.0.0.1", 9001);

    const char    *msg     = "Hello from PVM over UDP!";
    const uint8_t *payload = (const uint8_t *)msg;
    size_t         pay_len = strlen(msg);

    printf("\n  Sending:  \"%s\"\n", msg);
    dump_hex("payload", payload, pay_len);

    int sent = pvm_send(payload, pay_len);
    if (sent < 0)
        printf("  [WARN] pvm_send returned error.\n");
    else
        printf("  pvm_send -> %d bytes sent.\n", sent);

    uint8_t rx[256];
    int     n = pvm_receive(rx, sizeof(rx));
    if (n > 0) {
        rx[n] = '\0';
        printf("  pvm_receive -> %d bytes: \"%s\"\n", n, (char *)rx);
        dump_hex("received", rx, (size_t)n);
    } else {
        printf("  pvm_receive -> no data (loopback may need brief delay).\n");
    }
}

/* --- Demo 2: VESPER-LITE module ----------------------------------------- */
static void demo_vesper_lite(void)
{
    demo_separator("DEMO 2 - VESPER-LITE Protocol Module");

    if (pvm_switch("vesper_lite") != 0) {
        fprintf(stderr, "  [FAIL] Could not switch to vesper_lite module.\n");
        return;
    }

    pvm_connect("127.0.0.1", 9001);

    const char    *msg     = "VESPER payload: status=OK";
    const uint8_t *payload = (const uint8_t *)msg;
    size_t         pay_len = strlen(msg);

    printf("\n  Sending:  \"%s\"\n", msg);
    printf("  Wire layout: [0x02 proto_id][0x01 version][0x01 type][len BE][payload]\n");
    dump_hex("payload", payload, pay_len);

    int sent = pvm_send(payload, pay_len);
    if (sent < 0)
        printf("  [WARN] pvm_send returned error.\n");
    else
        printf("  pvm_send -> %d bytes sent.\n", sent);

    uint8_t rx[256];
    int     n = pvm_receive(rx, sizeof(rx));
    if (n > 0) {
        rx[n] = '\0';
        printf("  pvm_receive -> %d bytes: \"%s\"\n", n, (char *)rx);
        dump_hex("received", rx, (size_t)n);
    } else {
        printf("  pvm_receive -> no data.\n");
    }
}

/* --- Demo 3: Runtime protocol switch ------------------------------------ */
static void demo_runtime_switch(void)
{
    demo_separator("DEMO 3 - Runtime Protocol Switch");

    printf("\n  Modules currently loaded:\n");
    pvm_list_modules();

    printf("\n  Switching to UDP ...\n");
    pvm_switch("udp");
    pvm_list_modules();

    printf("\n  Switching to VESPER-LITE ...\n");
    pvm_switch("vesper_lite");
    pvm_list_modules();

    printf("\n  Switching back to UDP ...\n");
    pvm_switch("udp");
    pvm_list_modules();
}

/* --- Demo 4: Dispatcher routing ----------------------------------------- */
static void demo_dispatcher(void)
{
    demo_separator("DEMO 4 - Dispatcher & Wire-Frame Layout");

    printf("\n  PVM wire-frame format:\n");
    printf("    Byte 0:      proto_id  (0x01 = UDP, 0x02 = VESPER-LITE)\n");
    printf("    Bytes 1..N:  protocol payload\n\n");

    {
        const char *msg = "UDP frame data";
        uint8_t frame[256];
        frame[0] = PROTO_UDP;
        memcpy(frame + 1, msg, strlen(msg));
        size_t frame_len = 1 + strlen(msg);
        dump_hex("UDP   wire frame", frame, frame_len);
    }
    {
        const char *msg = "VESPER frame data";
        size_t      msg_len = strlen(msg);
        uint8_t     frame[256];

        frame[0] = PROTO_VESPER_LITE;
        frame[1] = VESPER_VERSION;
        frame[2] = VESPER_TYPE_DATA;
        frame[3] = (uint8_t)(msg_len >> 8);
        frame[4] = (uint8_t)(msg_len);
        memcpy(frame + 5, msg, msg_len);
        size_t frame_len = 5 + msg_len;
        dump_hex("VESPER wire frame", frame, frame_len);
    }

    printf("\n  Dispatcher routes on byte[0] -> calls registered handler.\n");
}

/* =========================================================================
 * NEW FEATURE DEMOS
 * ====================================================================== */

/* --- Demo 5: Adaptive Protocol Scheduler -------------------------------- */
static void demo_scheduler(void)
{
    demo_separator("DEMO 5 - Adaptive Protocol Scheduler");

    printf("\n  The scheduler automatically switches protocols based on\n");
    printf("  network conditions (latency, loss, bandwidth, jitter).\n\n");

    pvm_scheduler_init();

    /* Define scheduling rules. */
    printf("  Adding rules:\n");
    pvm_scheduler_add_rule(SCHED_METRIC_LATENCY_MS,  SCHED_CMP_LESS,
                           50, "vesper_lite", 10);
    pvm_scheduler_add_rule(SCHED_METRIC_LATENCY_MS,  SCHED_CMP_GREATER_EQ,
                           50, "udp", 5);
    pvm_scheduler_add_rule(SCHED_METRIC_LOSS_PCT,    SCHED_CMP_GREATER,
                           10, "vesper_lite", 20);

    printf("\n");
    pvm_scheduler_list_rules();

    /* Simulate changing network conditions. */
    printf("\n  --- Simulating low latency (20ms) ---\n");
    pvm_scheduler_update_metric(SCHED_METRIC_LATENCY_MS, 20);
    pvm_scheduler_update_metric(SCHED_METRIC_LOSS_PCT, 0);
    {
        SchedResult res = {0};
        pvm_scheduler_evaluate(&res);
        if (res.switched)
            printf("  -> Switched from '%s' to '%s'!\n",
                   res.from_proto, res.to_proto);
        else
            printf("  -> No switch needed (rule %d matched, already on target).\n",
                   res.matched_rule_index);
    }

    printf("\n  --- Simulating high latency (120ms) ---\n");
    pvm_scheduler_update_metric(SCHED_METRIC_LATENCY_MS, 120);
    {
        SchedResult res = {0};
        pvm_scheduler_evaluate(&res);
        if (res.switched)
            printf("  -> Switched from '%s' to '%s'!\n",
                   res.from_proto, res.to_proto);
        else
            printf("  -> No switch needed.\n");
    }

    printf("\n  --- Simulating packet loss (15%%) ---\n");
    pvm_scheduler_update_metric(SCHED_METRIC_LOSS_PCT, 15);
    {
        SchedResult res = {0};
        pvm_scheduler_evaluate(&res);
        if (res.switched)
            printf("  -> Switched from '%s' to '%s'! (loss rule overrides)\n",
                   res.from_proto, res.to_proto);
        else
            printf("  -> No switch needed.\n");
    }

    printf("\n  --- Simulating recovery (latency 30ms, loss 0%%) ---\n");
    pvm_scheduler_update_metric(SCHED_METRIC_LATENCY_MS, 30);
    pvm_scheduler_update_metric(SCHED_METRIC_LOSS_PCT, 0);
    {
        SchedResult res = {0};
        pvm_scheduler_evaluate(&res);
        if (res.switched)
            printf("  -> Switched from '%s' to '%s'! (recovered)\n",
                   res.from_proto, res.to_proto);
        else
            printf("  -> Already on optimal protocol.\n");
    }

    pvm_scheduler_shutdown();
}

/* --- Demo 6: Protocol Composition (Pipeline) ---------------------------- */
static void demo_pipeline(void)
{
    demo_separator("DEMO 6 - Protocol Composition (Stackable Layers)");

    printf("\n  Building a composable network stack:\n");
    printf("    [XOR Encrypt] -> [Checksum] -> [Transport]\n\n");

    pvm_pipeline_init();

    /* Make sure we have an active protocol for transport. */
    pvm_switch("udp");
    pvm_connect("127.0.0.1", 9001);

    /* Push layers. */
    pvm_pipeline_push("xor_encrypt");
    pvm_pipeline_push("checksum");

    printf("\n");
    pvm_pipeline_list();

    /* Send data through the pipeline. */
    const char    *msg     = "Secret message via pipeline!";
    const uint8_t *payload = (const uint8_t *)msg;
    size_t         pay_len = strlen(msg);

    printf("\n  Sending through pipeline: \"%s\"\n", msg);
    dump_hex("original payload", payload, pay_len);

    int sent = pvm_pipeline_send(payload, pay_len);
    if (sent < 0)
        printf("  [WARN] pvm_pipeline_send returned error.\n");
    else
        printf("  pvm_pipeline_send -> %d bytes sent (encrypted+checksummed on wire).\n", sent);

    /* Receive and unwind the pipeline. */
    uint8_t rx[256];
    int     n = pvm_pipeline_receive(rx, sizeof(rx));
    if (n > 0) {
        rx[n] = '\0';
        printf("  pvm_pipeline_receive -> %d bytes: \"%s\"\n", n, (char *)rx);
        dump_hex("decrypted payload", rx, (size_t)n);
    } else {
        printf("  pvm_pipeline_receive -> no data.\n");
    }

    /* Show RLE compression layer too. */
    printf("\n  Adding RLE compression to the stack:\n");
    printf("    [XOR Encrypt] -> [Checksum] -> [RLE Compress] -> [Transport]\n");
    pvm_pipeline_push("rle_compress");
    pvm_pipeline_list();

    printf("\n  Sending 'AAAAAABBBBCC' (compressible) through pipeline:\n");
    const char *compressible = "AAAAAABBBBCC";
    dump_hex("original", (const uint8_t *)compressible, strlen(compressible));

    sent = pvm_pipeline_send((const uint8_t *)compressible, strlen(compressible));
    printf("  pvm_pipeline_send -> %d bytes (encrypted+checksummed+compressed).\n", sent);

    n = pvm_pipeline_receive(rx, sizeof(rx));
    if (n > 0) {
        rx[n] = '\0';
        printf("  pvm_pipeline_receive -> %d bytes: \"%s\"\n", n, (char *)rx);
    }

    pvm_pipeline_shutdown();
}

/* --- Demo 7: Protocol Bytecode VM --------------------------------------- */
static void demo_bytecode(void)
{
    demo_separator("DEMO 7 - Protocol Bytecode VM");

    printf("\n  Defining a VESPER-LITE-like protocol entirely in bytecode:\n");
    printf("  (No .so module needed - the protocol is a script!)\n\n");

    /* Build a send program that constructs a VESPER-LITE frame. */
    PvmBytecodeProgram send_prog;
    pvm_bc_program_init(&send_prog, "vesper_bc_send");

    pvm_bc_emit(&send_prog, OP_LOAD_HEADER,    0, 0);       /* reset frame */
    pvm_bc_emit(&send_prog, OP_SET_PROTO_ID,   0x02, 0);    /* proto_id = VESPER */
    pvm_bc_emit(&send_prog, OP_SET_FIELD_U8,   1, 0x01);    /* version = 0x01 */
    pvm_bc_emit(&send_prog, OP_SET_FIELD_U8,   2, 0x01);    /* type = DATA */
    pvm_bc_emit(&send_prog, OP_SET_PAYLOAD_LEN, 3, 0);      /* length at offset 3 */
    pvm_bc_emit(&send_prog, OP_APPEND_PAYLOAD, 0, 0);       /* append user data */
    pvm_bc_emit(&send_prog, OP_PRINT_FRAME,    0, 0);       /* debug print */
    pvm_bc_emit(&send_prog, OP_SEND_FRAME,     0, 0);       /* transmit */
    pvm_bc_emit(&send_prog, OP_HALT,           0, 0);       /* done */

    printf("  Send program disassembly:\n");
    pvm_bc_disassemble(&send_prog);

    /* Execute the bytecode send program. */
    const char    *msg     = "Bytecode says hello!";
    const uint8_t *payload = (const uint8_t *)msg;
    size_t         pay_len = strlen(msg);

    printf("\n  Executing bytecode send with payload: \"%s\"\n", msg);
    PvmBytecodeCtx send_ctx;
    int sent = pvm_bc_execute(&send_prog, payload, pay_len, &send_ctx);
    if (sent > 0)
        printf("  Bytecode sent %d bytes.\n", sent);
    else
        printf("  Bytecode send returned %d.\n", sent);

    /* Build a receive program that validates and extracts a VESPER frame. */
    PvmBytecodeProgram recv_prog;
    pvm_bc_program_init(&recv_prog, "vesper_bc_recv");

    pvm_bc_emit(&recv_prog, OP_RECV_FRAME,      0, 0);      /* read from PAL */
    pvm_bc_emit(&recv_prog, OP_PRINT_FRAME,     0, 0);      /* debug print */
    pvm_bc_emit(&recv_prog, OP_CHECK_FIELD_U8,  0, 0x02);   /* verify proto_id */
    pvm_bc_emit(&recv_prog, OP_CHECK_FIELD_U8,  1, 0x01);   /* verify version */
    pvm_bc_emit(&recv_prog, OP_EXTRACT_PAYLOAD, 5, 0);      /* payload starts at byte 5 */
    pvm_bc_emit(&recv_prog, OP_HALT,            0, 0);

    printf("\n  Receive program disassembly:\n");
    pvm_bc_disassemble(&recv_prog);

    printf("\n  Executing bytecode receive:\n");
    uint8_t rx[256];
    PvmBytecodeCtx recv_ctx;
    int n = pvm_bc_execute_recv(&recv_prog, rx, sizeof(rx), &recv_ctx);
    if (n > 0) {
        rx[n] = '\0';
        printf("  Bytecode received %d bytes: \"%s\"\n", n, (char *)rx);
    } else {
        printf("  Bytecode receive returned %d (frame may have been consumed already).\n", n);
    }
}

/* --- Demo 8: Network Simulation Mode ------------------------------------ */
static void demo_simulator(void)
{
    demo_separator("DEMO 8 - Network Simulation Mode");

    printf("\n  Running PVM without real sockets - pure in-memory simulation.\n");
    printf("  Configuring: 20%% packet loss, seed=42 for reproducibility.\n\n");

    SimConfig sim_cfg;
    memset(&sim_cfg, 0, sizeof(sim_cfg));
    sim_cfg.loss_pct = 20;
    sim_cfg.seed     = 42;

    pvm_sim_configure(&sim_cfg);

    /* Get the simulated PAL and use it directly for this demo. */
    const PlatformOps *sim_pal = pvm_sim_get_ops();
    sim_pal->init();

    printf("\n  Sending 20 frames through simulated network...\n");
    int delivered = 0;
    int total     = 20;

    for (int i = 0; i < total; ++i) {
        char msg[64];
        snprintf(msg, sizeof(msg), "SIM_FRAME_%02d", i);
        sim_pal->send_frame((const uint8_t *)msg, strlen(msg));
    }

    /* Receive all that made it through. */
    uint8_t rx[256];
    while (sim_pal->poll() > 0) {
        int n = sim_pal->recv_frame(rx, sizeof(rx));
        if (n > 0) {
            rx[n] = '\0';
            delivered++;
        }
    }

    SimStats st;
    pvm_sim_get_stats(&st);

    printf("  Results:\n");
    printf("    Frames sent:     %lu\n", (unsigned long)st.frames_sent);
    printf("    Frames received: %lu\n", (unsigned long)st.frames_received);
    printf("    Frames dropped:  %lu\n", (unsigned long)st.frames_dropped);
    printf("    Loss rate:       %.1f%%\n",
           st.frames_sent ? (100.0 * (double)st.frames_dropped / (double)st.frames_sent) : 0.0);
    printf("    Delivered:       %d / %d frames\n", delivered, total);

    sim_pal->shutdown();

    /* Demo with 0% loss. */
    printf("\n  Now with 0%% loss (perfect network):\n");
    sim_cfg.loss_pct = 0;
    pvm_sim_configure(&sim_cfg);
    sim_pal->init();
    pvm_sim_reset_stats();

    for (int i = 0; i < total; ++i) {
        char msg[64];
        snprintf(msg, sizeof(msg), "PERFECT_%02d", i);
        sim_pal->send_frame((const uint8_t *)msg, strlen(msg));
    }

    delivered = 0;
    while (sim_pal->poll() > 0) {
        int n = sim_pal->recv_frame(rx, sizeof(rx));
        if (n > 0) delivered++;
    }

    pvm_sim_get_stats(&st);
    printf("    Frames sent:     %lu\n", (unsigned long)st.frames_sent);
    printf("    Frames received: %lu\n", (unsigned long)st.frames_received);
    printf("    Frames dropped:  %lu\n", (unsigned long)st.frames_dropped);
    printf("    Delivered:       %d / %d frames (100%%)\n", delivered, total);

    sim_pal->shutdown();
}

/* --- Demo 9: Cross-Device Protocol Negotiation -------------------------- */
static void demo_negotiation(void)
{
    demo_separator("DEMO 9 - Cross-Device Protocol Negotiation");

    printf("\n  Simulating two devices discovering common protocols.\n\n");

    pvm_nego_init();

    /* Simulate Device A's capabilities. */
    NegoCapabilities device_a;
    memset(&device_a, 0, sizeof(device_a));
    strncpy(device_a.names[0], "udp", NEGO_NAME_WIDTH - 1);
    strncpy(device_a.names[1], "vesper_lite", NEGO_NAME_WIDTH - 1);
    strncpy(device_a.names[2], "quic_lite", NEGO_NAME_WIDTH - 1);
    device_a.count = 3;

    /* Simulate Device B's capabilities. */
    NegoCapabilities device_b;
    memset(&device_b, 0, sizeof(device_b));
    strncpy(device_b.names[0], "tcp_lite", NEGO_NAME_WIDTH - 1);
    strncpy(device_b.names[1], "vesper_lite", NEGO_NAME_WIDTH - 1);
    strncpy(device_b.names[2], "udp", NEGO_NAME_WIDTH - 1);
    device_b.count = 3;

    pvm_nego_print_caps("Device A", &device_a);
    pvm_nego_print_caps("Device B", &device_b);

    /* Serialize Device A's offer. */
    uint8_t wire[512];
    int wire_len = pvm_nego_serialize_caps(&device_a, NEGO_OFFER,
                                           wire, sizeof(wire));
    printf("\n  Device A serialized offer (%d bytes):\n", wire_len);
    dump_hex("wire", wire, (size_t)wire_len);

    /* Device B deserializes and finds common protocol. */
    uint8_t msg_type;
    NegoCapabilities received_caps;
    pvm_nego_deserialize_caps(wire, (size_t)wire_len,
                              &msg_type, &received_caps);

    printf("\n  Device B received OFFER, deserializing...\n");
    pvm_nego_print_caps("Received from A", &received_caps);

    /* Find mutual protocol. */
    char common[NEGO_NAME_WIDTH];
    if (pvm_nego_find_common(&device_b, &received_caps, common) == 0) {
        printf("\n  >>> Negotiation SUCCESS: agreed on '%s' <<<\n", common);
        printf("  Both devices would now call pvm_switch(\"%s\")\n", common);
    } else {
        printf("\n  >>> Negotiation FAILED: no common protocol <<<\n");
    }

    /* Demo: no overlap scenario. */
    printf("\n  --- Testing with incompatible devices ---\n");
    NegoCapabilities device_c;
    memset(&device_c, 0, sizeof(device_c));
    strncpy(device_c.names[0], "alien_proto", NEGO_NAME_WIDTH - 1);
    strncpy(device_c.names[1], "quantum_net", NEGO_NAME_WIDTH - 1);
    device_c.count = 2;

    pvm_nego_print_caps("Device C", &device_c);

    if (pvm_nego_find_common(&device_a, &device_c, common) == 0) {
        printf("  Match found: '%s'\n", common);
    } else {
        printf("  >>> No common protocol - REJECT <<<\n");
    }

    pvm_nego_shutdown();
}

/* =========================================================================
 * main
 * ====================================================================== */
int main(void)
{
    printf("\n");
    printf("+--------------------------------------------------------------+\n");
    printf("|          VESPER OS - Protocol Virtual Machine (PVM)           |\n");
    printf("|         Cross-Platform Adaptive Networking Runtime            |\n");
    printf("|                                                              |\n");
    printf("|  Features:                                                   |\n");
    printf("|    [1] Dynamic Protocol Modules (.so/.dll)                   |\n");
    printf("|    [2] Adaptive Protocol Scheduler                           |\n");
    printf("|    [3] Composable Network Stack (Pipeline)                   |\n");
    printf("|    [4] Protocol Bytecode VM                                  |\n");
    printf("|    [5] Network Simulation Engine                             |\n");
    printf("|    [6] Cross-Device Protocol Negotiation                     |\n");
    printf("+--------------------------------------------------------------+\n");

    /* === STEP 1: Initialise the PVM runtime === */
    demo_separator("STEP 1 - PVM Runtime Initialisation");
    if (pvm_init() != 0) {
        fprintf(stderr, "[FATAL] PVM initialisation failed.\n");
        return 1;
    }

    /* === STEP 2: Load protocol modules === */
    demo_separator("STEP 2 - Loading Protocol Modules");
    printf("\n  pvm_load(\"udp\") ...\n");
    if (pvm_load("udp") != 0)
        fprintf(stderr, "  [WARN] UDP module not found - "
                        "run 'make modules' first.\n");

    printf("\n  pvm_load(\"vesper_lite\") ...\n");
    if (pvm_load("vesper_lite") != 0)
        fprintf(stderr, "  [WARN] VESPER-LITE module not found - "
                        "run 'make modules' first.\n");

    printf("\n  Loaded modules after pvm_load calls:\n");
    pvm_list_modules();

    /* === STEP 3: Original baseline demos === */
    demo_udp();
    demo_vesper_lite();
    demo_runtime_switch();
    demo_dispatcher();

    /* === STEP 4: New revolutionary features === */
    demo_scheduler();
    demo_pipeline();
    demo_bytecode();
    demo_simulator();
    demo_negotiation();

    /* === STEP 5: Unload and shutdown === */
    demo_separator("STEP 5 - Unloading & Shutdown");
    printf("\n  pvm_unload(\"udp\") ...\n");
    pvm_unload("udp");
    printf("\n  Remaining modules:\n");
    pvm_list_modules();
    printf("\n");
    pvm_shutdown();

    printf("\n  Demo complete. All 5 revolutionary features demonstrated.\n\n");
    return 0;
}

