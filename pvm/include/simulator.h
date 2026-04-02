/**
 * simulator.h — Network Simulation Mode Interface
 *
 * Provides a simulated PAL (Platform Abstraction Layer) that requires
 * no real OS sockets.  Instead, packets are looped back through an
 * in-memory ring buffer with configurable impairments:
 *
 *   • Packet loss      — random percentage of frames are silently dropped
 *   • Latency injection— frames are delayed by a configurable interval
 *   • Bandwidth cap    — excess frames beyond a throughput limit are dropped
 *   • Jitter           — random variation added to latency
 *
 * This turns the PVM into a built-in network lab / chaos testing engine.
 *
 * Example:
 *   pvm_sim_configure(sim_cfg);
 *   pvm_sim_enable();        // replaces real PAL with simulator
 *   pvm_send(data, len);     // goes through simulated network
 *   pvm_sim_get_stats(&st);  // inspect dropped / delayed counts
 *   pvm_sim_disable();       // restore real PAL
 */
#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <stdint.h>
#include <stddef.h>
#include "platform.h"

/** Maximum frames the simulation ring buffer can hold. */
#define SIM_RING_CAPACITY  256

/** Maximum bytes per simulated frame. */
#define SIM_FRAME_MAX      65536u

/* -------------------------------------------------------------------------
 * Simulation configuration
 * ---------------------------------------------------------------------- */
typedef struct {
    int      loss_pct;         /**< Packet loss percentage (0–100).         */
    int      latency_ms;       /**< Fixed latency in milliseconds.          */
    int      jitter_ms;        /**< Random jitter added to latency (±).     */
    int      bandwidth_kbps;   /**< Bandwidth cap in Kbit/s (0 = unlimited).*/
    uint32_t seed;             /**< PRNG seed (0 = use clock).              */
} SimConfig;

/* -------------------------------------------------------------------------
 * Simulation statistics
 * ---------------------------------------------------------------------- */
typedef struct {
    uint64_t frames_sent;       /**< Total frames submitted to simulator.   */
    uint64_t frames_received;   /**< Total frames successfully received.    */
    uint64_t frames_dropped;    /**< Frames lost to simulated impairments.  */
    uint64_t bytes_sent;        /**< Total bytes submitted.                 */
    uint64_t bytes_received;    /**< Total bytes delivered.                 */
} SimStats;

/* =========================================================================
 * Public API
 * ====================================================================== */

/**
 * pvm_sim_configure — Set simulation parameters.
 *
 * Can be called before or after pvm_sim_enable(); changes take effect
 * immediately.
 */
void pvm_sim_configure(const SimConfig *cfg);

/**
 * pvm_sim_enable — Replace the real PAL with the simulated PAL.
 *
 * After this call, all pvm_send / pvm_receive traffic goes through the
 * in-memory simulator rather than real OS sockets.
 *
 * @return  0 on success, -1 on failure.
 */
int pvm_sim_enable(void);

/**
 * pvm_sim_disable — Restore the original (real) PAL.
 *
 * @return  0 on success, -1 if the simulator was not active.
 */
int pvm_sim_disable(void);

/**
 * pvm_sim_get_stats — Retrieve simulation statistics.
 */
void pvm_sim_get_stats(SimStats *out);

/**
 * pvm_sim_reset_stats — Zero all counters.
 */
void pvm_sim_reset_stats(void);

/**
 * pvm_sim_is_enabled — Check if simulation mode is active.
 * @return  1 if enabled, 0 if not.
 */
int pvm_sim_is_enabled(void);

/**
 * pvm_sim_get_ops — Return the simulator's PlatformOps vtable.
 * (Used internally by pvm_sim_enable; also available for direct use.)
 */
const PlatformOps *pvm_sim_get_ops(void);

#endif /* SIMULATOR_H */
