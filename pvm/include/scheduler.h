/**
 * scheduler.h — Adaptive Protocol Scheduling Engine
 *
 * The scheduler monitors network conditions and automatically switches
 * the active protocol based on user-defined rules.  This turns the PVM
 * from a manual-switch framework into an adaptive networking runtime.
 *
 * Each rule is a (metric, comparator, threshold, target_protocol) tuple.
 * When pvm_scheduler_evaluate() is called, the scheduler checks every
 * rule against the current network metrics and triggers a pvm_switch()
 * to the highest-priority matching rule's target protocol.
 *
 * Example:
 *   pvm_scheduler_add_rule(SCHED_METRIC_LATENCY, SCHED_CMP_LESS,
 *                          50, "vesper_lite", 1);
 *   pvm_scheduler_add_rule(SCHED_METRIC_LOSS_PCT, SCHED_CMP_GREATER,
 *                          5, "reliable", 2);
 *   pvm_scheduler_evaluate();   // auto-switches if a rule matches
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stddef.h>

/** Maximum number of scheduling rules. */
#define SCHED_MAX_RULES  32

/* -------------------------------------------------------------------------
 * Network metric identifiers
 *
 * The scheduler evaluates rules against these metrics.  Metric values
 * are supplied by the application (or future instrumentation hooks)
 * via pvm_scheduler_update_metric().
 * ---------------------------------------------------------------------- */
typedef enum {
    SCHED_METRIC_LATENCY_MS  = 0, /**< Round-trip latency in milliseconds.   */
    SCHED_METRIC_LOSS_PCT    = 1, /**< Packet loss as integer percentage.    */
    SCHED_METRIC_BW_KBPS     = 2, /**< Available bandwidth in Kbit/s.        */
    SCHED_METRIC_JITTER_MS   = 3, /**< Jitter (latency variance) in ms.      */
    SCHED_METRIC_CUSTOM_1    = 4, /**< User-defined custom metric #1.        */
    SCHED_METRIC_CUSTOM_2    = 5, /**< User-defined custom metric #2.        */
    SCHED_METRIC__COUNT      = 6, /**< Total number of metric slots.         */
} SchedMetric;

/* -------------------------------------------------------------------------
 * Comparison operators for rule evaluation
 * ---------------------------------------------------------------------- */
typedef enum {
    SCHED_CMP_LESS         = 0, /**< metric < threshold                    */
    SCHED_CMP_LESS_EQ      = 1, /**< metric <= threshold                   */
    SCHED_CMP_GREATER      = 2, /**< metric > threshold                    */
    SCHED_CMP_GREATER_EQ   = 3, /**< metric >= threshold                   */
    SCHED_CMP_EQUAL        = 4, /**< metric == threshold                   */
    SCHED_CMP_NOT_EQUAL    = 5, /**< metric != threshold                   */
} SchedComparator;

/* -------------------------------------------------------------------------
 * Scheduling rule
 * ---------------------------------------------------------------------- */
typedef struct {
    SchedMetric     metric;         /**< Which metric to evaluate.           */
    SchedComparator comparator;     /**< How to compare metric vs threshold. */
    int32_t         threshold;      /**< Threshold value.                    */
    char            target[64];     /**< Protocol module name to switch to.  */
    int             priority;       /**< Higher = evaluated first.           */
    int             enabled;        /**< 1 = active, 0 = disabled.          */
} SchedRule;

/* -------------------------------------------------------------------------
 * Result of the last evaluation
 * ---------------------------------------------------------------------- */
typedef struct {
    int  switched;                  /**< 1 if a switch occurred, 0 if not.  */
    char from_proto[64];            /**< Previous protocol name (if switched). */
    char to_proto[64];              /**< New protocol name (if switched).      */
    int  matched_rule_index;        /**< Index of the matching rule (-1 = none). */
} SchedResult;

/* =========================================================================
 * Public API
 * ====================================================================== */

/**
 * pvm_scheduler_init — Initialise the scheduling engine.
 * @return  0 on success, -1 on failure.
 */
int pvm_scheduler_init(void);

/**
 * pvm_scheduler_shutdown — Release scheduler state.
 */
void pvm_scheduler_shutdown(void);

/**
 * pvm_scheduler_add_rule — Register a scheduling rule.
 *
 * @param metric     Which network metric to evaluate.
 * @param cmp        Comparison operator.
 * @param threshold  Threshold value for the comparison.
 * @param target     Protocol module name to switch to on match.
 * @param priority   Rule priority (higher = evaluated first).
 * @return           Rule index on success, -1 if the table is full.
 */
int pvm_scheduler_add_rule(SchedMetric metric, SchedComparator cmp,
                           int32_t threshold, const char *target,
                           int priority);

/**
 * pvm_scheduler_remove_rule — Remove a rule by index.
 */
void pvm_scheduler_remove_rule(int rule_index);

/**
 * pvm_scheduler_update_metric — Supply a new value for a network metric.
 *
 * @param metric  Which metric to update.
 * @param value   New metric value.
 */
void pvm_scheduler_update_metric(SchedMetric metric, int32_t value);

/**
 * pvm_scheduler_get_metric — Read the current value of a metric.
 */
int32_t pvm_scheduler_get_metric(SchedMetric metric);

/**
 * pvm_scheduler_evaluate — Evaluate all rules and auto-switch if needed.
 *
 * Evaluates rules in priority order (highest first).  The first matching
 * rule triggers a pvm_switch() to its target protocol.
 *
 * @param result  Optional — filled with the outcome of the evaluation.
 * @return        0 on success (regardless of whether a switch occurred),
 *                -1 on error.
 */
int pvm_scheduler_evaluate(SchedResult *result);

/**
 * pvm_scheduler_list_rules — Print all registered rules to stdout.
 */
void pvm_scheduler_list_rules(void);

/**
 * pvm_scheduler_metric_name — Human-readable metric name.
 */
const char *pvm_scheduler_metric_name(SchedMetric m);

/**
 * pvm_scheduler_cmp_name — Human-readable comparator name.
 */
const char *pvm_scheduler_cmp_name(SchedComparator c);

#endif /* SCHEDULER_H */
