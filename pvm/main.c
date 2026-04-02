/**
 * main.c — VESPER OS Protocol Virtual Machine — CLI Demo
 *
 * Demonstrates the full PVM lifecycle:
 *   1. Runtime initialisation (PAL auto-selected at compile time)
 *   2. Dynamic module loading  (pvm_load)
 *   3. Protocol switching      (pvm_switch)
 *   4. Connecting and sending  (pvm_connect / pvm_send)
 *   5. Receiving own frames    (loopback — same socket, same port)
 *   6. Clean shutdown          (pvm_shutdown)
 *
 * The demo uses loopback (127.0.0.1) so no remote peer is needed.
 * Both UDP and VESPER-LITE modules are exercised.
 *
 * Build:
 *   make          — auto-detects OS, builds modules and binary
 *
 * Run:
 *   make test     — builds and runs this demo
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "pvm.h"
#include "packet.h"   /* proto_id_name, vesper_type_name, etc. */

/* -------------------------------------------------------------------------
 * Utility — pretty-print a byte buffer
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
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  %s\n", title);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

/* ─── Demo 1: UDP module ───────────────────────────────────────────────── */
static void demo_udp(void)
{
    demo_separator("DEMO 1 — UDP Protocol Module");

    /* Make UDP the active protocol. */
    if (pvm_switch("udp") != 0) {
        fprintf(stderr, "  [FAIL] Could not switch to UDP module.\n");
        return;
    }

    /* Point at our own loopback address (same socket → loopback receive). */
    pvm_connect("127.0.0.1", 9001);

    /* ── Send ── */
    const char    *msg     = "Hello from PVM over UDP!";
    const uint8_t *payload = (const uint8_t *)msg;
    size_t         pay_len = strlen(msg);

    printf("\n  Sending:  \"%s\"\n", msg);
    dump_hex("payload", payload, pay_len);

    int sent = pvm_send(payload, pay_len);
    if (sent < 0)
        printf("  [WARN] pvm_send returned error (no remote peer? normal in CI).\n");
    else
        printf("  pvm_send → %d bytes sent.\n", sent);

    /* ── Receive (loopback) ── */
    uint8_t rx[256];
    int     n = pvm_receive(rx, sizeof(rx));
    if (n > 0) {
        rx[n] = '\0';
        printf("  pvm_receive → %d bytes: \"%s\"\n", n, (char *)rx);
        dump_hex("received", rx, (size_t)n);
    } else {
        printf("  pvm_receive → no data (loopback may need brief delay).\n");
    }
}

/* ─── Demo 2: VESPER-LITE module ──────────────────────────────────────── */
static void demo_vesper_lite(void)
{
    demo_separator("DEMO 2 — VESPER-LITE Protocol Module");

    /* Switch to VESPER-LITE. */
    if (pvm_switch("vesper_lite") != 0) {
        fprintf(stderr, "  [FAIL] Could not switch to vesper_lite module.\n");
        return;
    }

    pvm_connect("127.0.0.1", 9001);

    /* ── Send a DATA frame ── */
    const char    *msg     = "VESPER payload: status=OK";
    const uint8_t *payload = (const uint8_t *)msg;
    size_t         pay_len = strlen(msg);

    printf("\n  Sending:  \"%s\"\n", msg);
    printf("  Wire layout: [0x02 proto_id][0x01 version][0x01 type][len BE][payload]\n");
    dump_hex("payload", payload, pay_len);

    int sent = pvm_send(payload, pay_len);
    if (sent < 0)
        printf("  [WARN] pvm_send returned error (no remote peer? normal in CI).\n");
    else
        printf("  pvm_send → %d bytes sent.\n", sent);

    /* ── Receive (loopback) ── */
    uint8_t rx[256];
    int     n = pvm_receive(rx, sizeof(rx));
    if (n > 0) {
        rx[n] = '\0';
        printf("  pvm_receive → %d bytes: \"%s\"\n", n, (char *)rx);
        dump_hex("received", rx, (size_t)n);
    } else {
        printf("  pvm_receive → no data (loopback may need brief delay).\n");
    }
}

/* ─── Demo 3: Runtime protocol switch ────────────────────────────────── */
static void demo_runtime_switch(void)
{
    demo_separator("DEMO 3 — Runtime Protocol Switch");

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

/* ─── Demo 4: Dispatcher routing demonstration ────────────────────────── */
static void demo_dispatcher(void)
{
    demo_separator("DEMO 4 — Dispatcher & Wire-Frame Layout");

    printf("\n  PVM wire-frame format:\n");
    printf("    Byte 0:      proto_id  (0x01 = UDP, 0x02 = VESPER-LITE)\n");
    printf("    Bytes 1…N:  protocol payload\n\n");

    /* Show what a UDP frame looks like. */
    {
        const char *msg = "UDP frame data";
        uint8_t frame[256];
        frame[0] = PROTO_UDP;
        memcpy(frame + 1, msg, strlen(msg));
        size_t frame_len = 1 + strlen(msg);
        dump_hex("UDP   wire frame", frame, frame_len);
    }

    /* Show what a VESPER-LITE frame looks like. */
    {
        const char *msg = "VESPER frame data";
        size_t      msg_len = strlen(msg);
        uint8_t     frame[256];

        frame[0] = PROTO_VESPER_LITE;      /* PVM proto_id */
        frame[1] = VESPER_VERSION;          /* VESPER header: version */
        frame[2] = VESPER_TYPE_DATA;        /* VESPER header: type    */
        frame[3] = (uint8_t)(msg_len >> 8); /* VESPER header: len hi  */
        frame[4] = (uint8_t)(msg_len);      /* VESPER header: len lo  */
        memcpy(frame + 5, msg, msg_len);
        size_t frame_len = 5 + msg_len;
        dump_hex("VESPER wire frame", frame, frame_len);
    }

    printf("\n  Dispatcher routes on byte[0] → calls registered handler.\n");
    printf("  Handlers receive a PvmPacket with data pointing at bytes[1…N].\n");
}

/* =========================================================================
 * main
 * ====================================================================== */
int main(void)
{
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║          VESPER OS — Protocol Virtual Machine (PVM)         ║\n");
    printf("║              Cross-Platform Networking Runtime               ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    /* ── Step 1: Initialise the PVM runtime ── */
    demo_separator("STEP 1 — PVM Runtime Initialisation");
    if (pvm_init() != 0) {
        fprintf(stderr, "[FATAL] PVM initialisation failed.\n");
        return 1;
    }

    /* ── Step 2: Load protocol modules ── */
    demo_separator("STEP 2 — Loading Protocol Modules");
    printf("\n  pvm_load(\"udp\") ...\n");
    if (pvm_load("udp") != 0)
        fprintf(stderr, "  [WARN] UDP module not found — "
                        "run 'make modules' first.\n");

    printf("\n  pvm_load(\"vesper_lite\") ...\n");
    if (pvm_load("vesper_lite") != 0)
        fprintf(stderr, "  [WARN] VESPER-LITE module not found — "
                        "run 'make modules' first.\n");

    printf("\n  Loaded modules after pvm_load calls:\n");
    pvm_list_modules();

    /* ── Step 3: Run demos ── */
    demo_udp();
    demo_vesper_lite();
    demo_runtime_switch();
    demo_dispatcher();

    /* ── Step 4: Unload individual module (optional) ── */
    demo_separator("STEP 4 — Unloading a Module");
    printf("\n  pvm_unload(\"udp\") ...\n");
    pvm_unload("udp");
    printf("\n  Remaining modules:\n");
    pvm_list_modules();

    /* ── Step 5: Shutdown ── */
    demo_separator("STEP 5 — PVM Shutdown");
    printf("\n");
    pvm_shutdown();

    printf("\n  ✔  Demo complete.\n\n");
    return 0;
}
