/**
 * core/pipeline.c - Protocol Composition / Stackable Layer Pipeline
 *
 * Implements a composable stack of transformation layers that process
 * data on the send path (top to bottom) and receive path (bottom to top).
 *
 * Built-in layers:
 *   "xor_encrypt"  - XOR cipher with key=0x42 (demo-grade encryption)
 *   "rle_compress" - Run-Length Encoding compression
 *   "checksum"     - Appends / verifies a 1-byte XOR checksum
 *
 * No OS-specific code.
 */

#include <stdio.h>
#include <string.h>

#include "pipeline.h"
#include "pvm.h"

/* -------------------------------------------------------------------------
 * Layer stack
 * ---------------------------------------------------------------------- */

static const PipelineLayer *layer_stack[PIPELINE_MAX_LAYERS];
static int   layer_count   = 0;
static int   pipe_inited   = 0;

/* Ping-pong buffers for chaining transforms without extra allocation. */
static uint8_t buf_a[PIPELINE_BUF_SIZE];
static uint8_t buf_b[PIPELINE_BUF_SIZE];

/* =========================================================================
 * Built-in layers
 * ====================================================================== */

/* --- XOR Encrypt -------------------------------------------------------- */
#define XOR_KEY  0x42u

static int xor_send(const uint8_t *in, size_t in_len,
                    uint8_t *out, size_t *out_len)
{
    if (in_len > PIPELINE_BUF_SIZE) return -1;
    for (size_t i = 0; i < in_len; ++i)
        out[i] = in[i] ^ XOR_KEY;
    *out_len = in_len;
    return 0;
}

static int xor_recv(const uint8_t *in, size_t in_len,
                    uint8_t *out, size_t *out_len)
{
    /* XOR is its own inverse. */
    return xor_send(in, in_len, out, out_len);
}

static const PipelineLayer layer_xor_encrypt = {
    .name           = "xor_encrypt",
    .transform_send = xor_send,
    .transform_recv = xor_recv,
};

/* --- RLE Compress ------------------------------------------------------- */

static int rle_send(const uint8_t *in, size_t in_len,
                    uint8_t *out, size_t *out_len)
{
    /* Simple RLE: [count][byte] pairs.  Runs capped at 255. */
    size_t j = 0;
    for (size_t i = 0; i < in_len; ) {
        uint8_t val   = in[i];
        size_t  count = 1;
        while (i + count < in_len && in[i + count] == val && count < 255)
            ++count;
        if (j + 2 > PIPELINE_BUF_SIZE) return -1;
        out[j++] = (uint8_t)count;
        out[j++] = val;
        i += count;
    }
    *out_len = j;
    return 0;
}

static int rle_recv(const uint8_t *in, size_t in_len,
                    uint8_t *out, size_t *out_len)
{
    size_t j = 0;
    for (size_t i = 0; i + 1 < in_len; i += 2) {
        uint8_t count = in[i];
        uint8_t val   = in[i + 1];
        if (j + count > PIPELINE_BUF_SIZE) return -1;
        memset(out + j, val, count);
        j += count;
    }
    *out_len = j;
    return 0;
}

static const PipelineLayer layer_rle_compress = {
    .name           = "rle_compress",
    .transform_send = rle_send,
    .transform_recv = rle_recv,
};

/* --- Checksum ----------------------------------------------------------- */

static int checksum_send(const uint8_t *in, size_t in_len,
                         uint8_t *out, size_t *out_len)
{
    if (in_len + 1 > PIPELINE_BUF_SIZE) return -1;
    memcpy(out, in, in_len);
    uint8_t cksum = 0;
    for (size_t i = 0; i < in_len; ++i)
        cksum ^= in[i];
    out[in_len] = cksum;
    *out_len = in_len + 1;
    return 0;
}

static int checksum_recv(const uint8_t *in, size_t in_len,
                         uint8_t *out, size_t *out_len)
{
    if (in_len < 1) return -1;
    size_t payload_len = in_len - 1;
    uint8_t expected = in[payload_len];
    uint8_t actual = 0;
    for (size_t i = 0; i < payload_len; ++i)
        actual ^= in[i];
    if (actual != expected) {
        fprintf(stderr, "[Pipeline:checksum] Mismatch: expected 0x%02X, got 0x%02X\n",
                expected, actual);
        return -1;
    }
    memcpy(out, in, payload_len);
    *out_len = payload_len;
    return 0;
}

static const PipelineLayer layer_checksum = {
    .name           = "checksum",
    .transform_send = checksum_send,
    .transform_recv = checksum_recv,
};

/* -------------------------------------------------------------------------
 * Built-in layer lookup table
 * ---------------------------------------------------------------------- */

typedef struct {
    const char          *name;
    const PipelineLayer *layer;
} BuiltinEntry;

static const BuiltinEntry builtins[] = {
    { "xor_encrypt",  &layer_xor_encrypt  },
    { "rle_compress", &layer_rle_compress  },
    { "checksum",     &layer_checksum      },
    { NULL, NULL },
};

/* =========================================================================
 * Public API
 * ====================================================================== */

int pvm_pipeline_init(void)
{
    memset(layer_stack, 0, sizeof(layer_stack));
    layer_count = 0;
    pipe_inited = 1;
    printf("[Pipeline] Initialised.\n");
    return 0;
}

void pvm_pipeline_shutdown(void)
{
    layer_count = 0;
    pipe_inited = 0;
    printf("[Pipeline] Shut down.\n");
}

int pvm_pipeline_push(const char *name)
{
    if (!pipe_inited || !name) return -1;
    if (layer_count >= PIPELINE_MAX_LAYERS) {
        fprintf(stderr, "[Pipeline] Stack full.\n");
        return -1;
    }

    for (const BuiltinEntry *e = builtins; e->name; ++e) {
        if (strcmp(e->name, name) == 0) {
            layer_stack[layer_count++] = e->layer;
            printf("[Pipeline] Pushed layer '%s' (depth=%d)\n", name, layer_count);
            return 0;
        }
    }
    fprintf(stderr, "[Pipeline] Unknown built-in layer '%s'.\n", name);
    return -1;
}

int pvm_pipeline_push_custom(const PipelineLayer *layer)
{
    if (!pipe_inited || !layer) return -1;
    if (layer_count >= PIPELINE_MAX_LAYERS) {
        fprintf(stderr, "[Pipeline] Stack full.\n");
        return -1;
    }
    layer_stack[layer_count++] = layer;
    printf("[Pipeline] Pushed custom layer '%s' (depth=%d)\n",
           layer->name ? layer->name : "unnamed", layer_count);
    return 0;
}

void pvm_pipeline_pop(void)
{
    if (layer_count > 0) {
        --layer_count;
        printf("[Pipeline] Popped layer (depth=%d)\n", layer_count);
    }
}

void pvm_pipeline_clear(void)
{
    layer_count = 0;
    printf("[Pipeline] Cleared all layers.\n");
}

int pvm_pipeline_send(const uint8_t *data, size_t len)
{
    if (!pipe_inited) return -1;
    if (!data || len == 0) return 0;

    /* If no layers, pass through directly. */
    if (layer_count == 0)
        return pvm_send(data, len);

    /* Run the send path: apply layers in stack order (0 to N-1). */
    const uint8_t *cur_in  = data;
    size_t         cur_len = len;
    uint8_t       *cur_out;

    /* We alternate between buf_a and buf_b to avoid extra copies. */
    int out_idx = 0; /* 0 = buf_a, 1 = buf_b */

    for (int i = 0; i < layer_count; ++i) {
        cur_out = (out_idx == 0) ? buf_a : buf_b;
        size_t out_len = 0;

        if (layer_stack[i]->transform_send(cur_in, cur_len,
                                            cur_out, &out_len) != 0) {
            fprintf(stderr, "[Pipeline] Layer '%s' transform_send failed.\n",
                    layer_stack[i]->name);
            return -1;
        }

        cur_in  = cur_out;
        cur_len = out_len;
        out_idx = 1 - out_idx; /* flip */
    }

    /* Send the final transformed data. */
    return pvm_send(cur_in, cur_len);
}

int pvm_pipeline_receive(uint8_t *buffer, size_t max_len)
{
    if (!pipe_inited) return -1;

    /* If no layers, receive directly. */
    if (layer_count == 0)
        return pvm_receive(buffer, max_len);

    /* Receive raw data from the PVM. */
    int n = pvm_receive(buf_a, sizeof(buf_a));
    if (n <= 0) return n;

    /* Run the receive path: apply layers in REVERSE order (N-1 to 0). */
    const uint8_t *cur_in  = buf_a;
    size_t         cur_len = (size_t)n;
    uint8_t       *cur_out;
    int out_idx = 1; /* start with buf_b since we received into buf_a */

    for (int i = layer_count - 1; i >= 0; --i) {
        cur_out = (out_idx == 0) ? buf_a : buf_b;
        size_t out_len = 0;

        if (layer_stack[i]->transform_recv(cur_in, cur_len,
                                            cur_out, &out_len) != 0) {
            fprintf(stderr, "[Pipeline] Layer '%s' transform_recv failed.\n",
                    layer_stack[i]->name);
            return -1;
        }

        cur_in  = cur_out;
        cur_len = out_len;
        out_idx = 1 - out_idx;
    }

    /* Copy result to caller's buffer. */
    size_t copy_len = (cur_len < max_len) ? cur_len : max_len;
    memcpy(buffer, cur_in, copy_len);
    return (int)copy_len;
}

void pvm_pipeline_list(void)
{
    printf("[Pipeline] Layer stack (depth=%d / %d):\n",
           layer_count, PIPELINE_MAX_LAYERS);
    if (layer_count == 0) {
        printf("  (empty - pass-through)\n");
        return;
    }
    printf("  Application data\n");
    for (int i = 0; i < layer_count; ++i) {
        printf("       | [%d] %s\n", i, layer_stack[i]->name);
        printf("       v\n");
    }
    printf("  Transport (pvm_send / pvm_receive)\n");
}

int pvm_pipeline_depth(void)
{
    return layer_count;
}
