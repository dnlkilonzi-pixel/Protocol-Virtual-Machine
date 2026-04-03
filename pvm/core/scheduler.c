/**
 * core/scheduler.c — Adaptive Protocol Scheduling Engine
 *
 * Monitors network condition metrics and automatically switches the
 * active PVM protocol based on priority-ordered rules.
 *
 * Design:
 *   • Rules are stored in a flat array, sorted by descending priority
 *     at evaluation time.
 *   • Metrics are updated externally (by the application or future
 *     instrumentation hooks).
 *   • pvm_scheduler_evaluate() scans rules top-to-bottom and calls
 *     pvm_switch() for the first match.
 *
 * No OS-specific code — all PAL interaction goes through pvm.h.
 */

#include <stdio.h>
#include <string.h>

#include "scheduler.h"
#include "pvm.h"

/* -------------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------- */

static SchedRule rules[SCHED_MAX_RULES];
static int       rule_count    = 0;
static int32_t   metrics[SCHED_METRIC__COUNT];
static int       sched_inited  = 0;

/* Current active protocol name — tracked to detect actual switches. */
static char current_proto[64];

/* =========================================================================
 * Public API
 * ====================================================================== */

int pvm_scheduler_init(void)
{
    memset(rules, 0, sizeof(rules));
    memset(metrics, 0, sizeof(metrics));
    rule_count   = 0;
    sched_inited = 1;
    current_proto[0] = '\0';
    printf("[Scheduler] Initialised.\n");
    return 0;
}

void pvm_scheduler_shutdown(void)
{
    rule_count   = 0;
    sched_inited = 0;
    printf("[Scheduler] Shut down.\n");
}

int pvm_scheduler_add_rule(SchedMetric metric, SchedComparator cmp,
                           int32_t threshold, const char *target,
                           int priority)
{
    if (!sched_inited) return -1;
    if (!target || *target == '\0') return -1;
    if (rule_count >= SCHED_MAX_RULES) {
        fprintf(stderr, "[Scheduler] Rule table full.\n");
        return -1;
    }

    SchedRule *r = &rules[rule_count];
    r->metric     = metric;
    r->comparator = cmp;
    r->threshold  = threshold;
    r->priority   = priority;
    r->enabled    = 1;
    strncpy(r->target, target, sizeof(r->target) - 1);
    r->target[sizeof(r->target) - 1] = '\0';

    int idx = rule_count++;
    printf("[Scheduler] Rule %d added: if %s %s %d → switch to '%s' (priority=%d)\n",
           idx,
           pvm_scheduler_metric_name(metric),
           pvm_scheduler_cmp_name(cmp),
           (int)threshold, target, priority);
    return idx;
}

void pvm_scheduler_remove_rule(int rule_index)
{
    if (rule_index < 0 || rule_index >= rule_count) return;
    rules[rule_index].enabled = 0;
    printf("[Scheduler] Rule %d disabled.\n", rule_index);
}

void pvm_scheduler_update_metric(SchedMetric metric, int32_t value)
{
    if ((int)metric < 0 || (int)metric >= SCHED_METRIC__COUNT) return;
    metrics[metric] = value;
}

int32_t pvm_scheduler_get_metric(SchedMetric metric)
{
    if ((int)metric < 0 || (int)metric >= SCHED_METRIC__COUNT) return 0;
    return metrics[metric];
}

/* -------------------------------------------------------------------------
 * Helper — evaluate a single comparison
 * ---------------------------------------------------------------------- */
static int eval_cmp(int32_t value, SchedComparator cmp, int32_t threshold)
{
    switch (cmp) {
        case SCHED_CMP_LESS:       return value <  threshold;
        case SCHED_CMP_LESS_EQ:    return value <= threshold;
        case SCHED_CMP_GREATER:    return value >  threshold;
        case SCHED_CMP_GREATER_EQ: return value >= threshold;
        case SCHED_CMP_EQUAL:      return value == threshold;
        case SCHED_CMP_NOT_EQUAL:  return value != threshold;
        default: return 0;
    }
}

/* -------------------------------------------------------------------------
 * Helper — simple insertion-sort of rule indices by descending priority
 * ---------------------------------------------------------------------- */
static void sort_rules_by_priority(int *indices, int n)
{
    for (int i = 1; i < n; ++i) {
        int key = indices[i];
        int j = i - 1;
        while (j >= 0 && rules[indices[j]].priority < rules[key].priority) {
            indices[j + 1] = indices[j];
            --j;
        }
        indices[j + 1] = key;
    }
}

int pvm_scheduler_evaluate(SchedResult *result)
{
    if (!sched_inited) return -1;

    /* Build a priority-sorted index list of enabled rules. */
    int sorted[SCHED_MAX_RULES];
    int sorted_count = 0;
    for (int i = 0; i < rule_count; ++i) {
        if (rules[i].enabled)
            sorted[sorted_count++] = i;
    }
    sort_rules_by_priority(sorted, sorted_count);

    /* Evaluate each rule in priority order. */
    for (int s = 0; s < sorted_count; ++s) {
        int i = sorted[s];
        const SchedRule *r = &rules[i];
        int32_t val = metrics[r->metric];

        if (eval_cmp(val, r->comparator, r->threshold)) {
            /* Rule matched — do we need to switch? */
            int already_active = (strcmp(current_proto, r->target) == 0);

            if (!already_active) {
                printf("[Scheduler] Rule %d matched: %s=%d %s %d → switching to '%s'\n",
                       i,
                       pvm_scheduler_metric_name(r->metric),
                       (int)val,
                       pvm_scheduler_cmp_name(r->comparator),
                       (int)r->threshold,
                       r->target);

                if (result) {
                    strncpy(result->from_proto, current_proto,
                            sizeof(result->from_proto) - 1);
                    result->from_proto[sizeof(result->from_proto) - 1] = '\0';
                }

                if (pvm_switch(r->target) == 0) {
                    strncpy(current_proto, r->target,
                            sizeof(current_proto) - 1);
                    current_proto[sizeof(current_proto) - 1] = '\0';

                    if (result) {
                        result->switched = 1;
                        strncpy(result->to_proto, r->target,
                                sizeof(result->to_proto) - 1);
                        result->to_proto[sizeof(result->to_proto) - 1] = '\0';
                        result->matched_rule_index = i;
                    }
                    return 0;
                }
            } else {
                /* Already on the right protocol. */
                if (result) {
                    result->switched = 0;
                    strncpy(result->to_proto, r->target,
                            sizeof(result->to_proto) - 1);
                    result->to_proto[sizeof(result->to_proto) - 1] = '\0';
                    result->matched_rule_index = i;
                }
                return 0;
            }
        }
    }

    /* No rule matched. */
    if (result) {
        result->switched = 0;
        result->matched_rule_index = -1;
    }
    return 0;
}

void pvm_scheduler_list_rules(void)
{
    printf("[Scheduler] Rules (%d / %d):\n", rule_count, SCHED_MAX_RULES);
    for (int i = 0; i < rule_count; ++i) {
        const SchedRule *r = &rules[i];
        printf("  [%d] %s: if %-12s %2s %5d → %-20s prio=%-3d %s\n",
               i,
               r->enabled ? "ON " : "OFF",
               pvm_scheduler_metric_name(r->metric),
               pvm_scheduler_cmp_name(r->comparator),
               (int)r->threshold,
               r->target,
               r->priority,
               (strcmp(current_proto, r->target) == 0) ? "<-- active" : "");
    }
    if (rule_count == 0)
        printf("  (none)\n");
}

const char *pvm_scheduler_metric_name(SchedMetric m)
{
    switch (m) {
        case SCHED_METRIC_LATENCY_MS:  return "latency_ms";
        case SCHED_METRIC_LOSS_PCT:    return "loss_pct";
        case SCHED_METRIC_BW_KBPS:     return "bw_kbps";
        case SCHED_METRIC_JITTER_MS:   return "jitter_ms";
        case SCHED_METRIC_CUSTOM_1:    return "custom_1";
        case SCHED_METRIC_CUSTOM_2:    return "custom_2";
        default:                       return "unknown";
    }
}

const char *pvm_scheduler_cmp_name(SchedComparator c)
{
    switch (c) {
        case SCHED_CMP_LESS:       return "<";
        case SCHED_CMP_LESS_EQ:    return "<=";
        case SCHED_CMP_GREATER:    return ">";
        case SCHED_CMP_GREATER_EQ: return ">=";
        case SCHED_CMP_EQUAL:      return "==";
        case SCHED_CMP_NOT_EQUAL:  return "!=";
        default:                   return "??";
    }
}
