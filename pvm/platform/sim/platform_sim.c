/**
 * platform/sim/platform_sim.c - Network Simulation PAL
 *
 * A simulated Platform Abstraction Layer that requires no real sockets.
 * Packets are looped back through an in-memory ring buffer with
 * configurable impairments:
 *
 *   - Packet loss:        random frames silently dropped
 *   - Latency injection:  not applicable in sync mode (noted in stats)
 *   - Bandwidth cap:      excess frames beyond a byte budget are dropped
 *   - Jitter:             random additional loss for high-jitter sim
 *
 * The simulator uses a simple linear congruential PRNG for deterministic
 * behaviour when seeded.
 *
 * No real OS socket calls are made in this file.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "simulator.h"

/* -------------------------------------------------------------------------
 * Ring buffer for in-memory packet loopback
 * ---------------------------------------------------------------------- */

typedef struct {
    uint8_t data[SIM_FRAME_MAX];
    size_t  len;
} SimFrame;

static SimFrame ring[SIM_RING_CAPACITY];
static int      ring_head  = 0;  /* Next write position. */
static int      ring_tail  = 0;  /* Next read position.  */
static int      ring_count = 0;  /* Frames in the buffer. */

/* -------------------------------------------------------------------------
 * Simulation state
 * ---------------------------------------------------------------------- */

static SimConfig  cfg;
static SimStats   stats;
static uint32_t   prng_state = 1;
static int        sim_inited = 0;

/* -------------------------------------------------------------------------
 * Simple PRNG (LCG) - deterministic when seeded
 * ---------------------------------------------------------------------- */
static uint32_t prng_next(void)
{
    prng_state = prng_state * 1103515245u + 12345u;
    return (prng_state >> 16) & 0x7FFFu;
}

static int prng_percent(void)
{
    return (int)(prng_next() % 100);
}

/* -------------------------------------------------------------------------
 * Simulated PAL functions
 * ---------------------------------------------------------------------- */

static int sim_init(void)
{
    ring_head  = 0;
    ring_tail  = 0;
    ring_count = 0;
    memset(&stats, 0, sizeof(stats));

    if (cfg.seed != 0) {
        prng_state = cfg.seed;
    } else {
        prng_state = (uint32_t)time(NULL);
    }

    sim_inited = 1;
    printf("[PAL:Sim] Simulated network initialised "
           "(loss=%d%%, latency=%dms, jitter=%dms, bw=%d kbps)\n",
           cfg.loss_pct, cfg.latency_ms, cfg.jitter_ms, cfg.bandwidth_kbps);
    return 0;
}

static int sim_send_frame(const uint8_t *data, size_t len)
{
    if (!sim_inited || !data || len == 0) return -1;
    if (len > SIM_FRAME_MAX) return -1;

    stats.frames_sent++;
    stats.bytes_sent += len;

    /* --- Packet loss simulation --- */
    if (cfg.loss_pct > 0 && prng_percent() < cfg.loss_pct) {
        stats.frames_dropped++;
        /* Frame silently dropped. */
        return (int)len; /* Appear successful to the sender. */
    }

    /* --- Bandwidth throttle (very simplified) --- */
    if (cfg.bandwidth_kbps > 0) {
        /* Simple model: if we have sent more than budget allows in this
         * "tick", drop the frame.  Real implementation would use timers.
         * For demo: drop if buffer is more than 75% full.               */
        if (ring_count > (SIM_RING_CAPACITY * 3 / 4)) {
            stats.frames_dropped++;
            return (int)len;
        }
    }

    /* --- Jitter simulation: additional random loss --- */
    if (cfg.jitter_ms > 0) {
        int jitter_loss = cfg.jitter_ms / 10; /* ~10% per 100ms jitter */
        if (jitter_loss > 0 && prng_percent() < jitter_loss) {
            stats.frames_dropped++;
            return (int)len;
        }
    }

    /* --- Enqueue frame into ring buffer --- */
    if (ring_count >= SIM_RING_CAPACITY) {
        /* Buffer full - drop oldest. */
        ring_tail = (ring_tail + 1) % SIM_RING_CAPACITY;
        --ring_count;
        stats.frames_dropped++;
    }

    SimFrame *f = &ring[ring_head];
    memcpy(f->data, data, len);
    f->len = len;
    ring_head = (ring_head + 1) % SIM_RING_CAPACITY;
    ++ring_count;

    return (int)len;
}

static int sim_recv_frame(uint8_t *buffer, size_t max_len)
{
    if (!sim_inited || ring_count <= 0) return -1;

    SimFrame *f = &ring[ring_tail];
    size_t copy_len = (f->len < max_len) ? f->len : max_len;
    memcpy(buffer, f->data, copy_len);

    ring_tail = (ring_tail + 1) % SIM_RING_CAPACITY;
    --ring_count;

    stats.frames_received++;
    stats.bytes_received += copy_len;

    return (int)copy_len;
}

static int sim_poll(void)
{
    if (!sim_inited) return -1;
    return (ring_count > 0) ? 1 : 0;
}

static void sim_shutdown(void)
{
    ring_count = 0;
    ring_head  = 0;
    ring_tail  = 0;
    sim_inited = 0;
    printf("[PAL:Sim] Simulation shutdown. Stats: sent=%lu recv=%lu dropped=%lu\n",
           (unsigned long)stats.frames_sent,
           (unsigned long)stats.frames_received,
           (unsigned long)stats.frames_dropped);
}

/* -------------------------------------------------------------------------
 * Simulator PlatformOps vtable
 * ---------------------------------------------------------------------- */

static const PlatformOps sim_ops = {
    .init       = sim_init,
    .send_frame = sim_send_frame,
    .recv_frame = sim_recv_frame,
    .poll       = sim_poll,
    .shutdown   = sim_shutdown,
};

/* =========================================================================
 * Public API
 * ====================================================================== */

void pvm_sim_configure(const SimConfig *c)
{
    if (!c) return;
    cfg = *c;
    printf("[Simulator] Configured: loss=%d%% latency=%dms jitter=%dms bw=%d kbps seed=%u\n",
           cfg.loss_pct, cfg.latency_ms, cfg.jitter_ms,
           cfg.bandwidth_kbps, cfg.seed);
}

/* The enable/disable mechanism stores the original PAL and swaps in
 * the simulator.  Since the PVM core obtains the PAL via
 * platform_get_ops(), we use a global flag to control which ops
 * are returned. */

static int sim_enabled = 0;

int pvm_sim_enable(void)
{
    if (sim_enabled) return 0;

    /* Initialise the simulated PAL. */
    if (sim_init() != 0) return -1;

    sim_enabled = 1;
    printf("[Simulator] Simulation mode ENABLED.\n");
    return 0;
}

int pvm_sim_disable(void)
{
    if (!sim_enabled) return -1;
    sim_shutdown();
    sim_enabled = 0;
    printf("[Simulator] Simulation mode DISABLED.\n");
    return 0;
}

void pvm_sim_get_stats(SimStats *out)
{
    if (out) *out = stats;
}

void pvm_sim_reset_stats(void)
{
    memset(&stats, 0, sizeof(stats));
}

int pvm_sim_is_enabled(void)
{
    return sim_enabled;
}

const PlatformOps *pvm_sim_get_ops(void)
{
    return &sim_ops;
}
