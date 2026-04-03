/**
 * pipeline.h — Protocol Composition / Stackable Layer Pipeline
 *
 * Allows layering / composing protocol transformations:
 *
 *   [Encryption Layer]
 *           ↓
 *   [Compression Layer]
 *           ↓
 *   [Transport Layer]
 *
 * Each layer implements a simple transform interface that processes
 * data on send (top → bottom) and on receive (bottom → top).  The
 * pipeline chains layers together; the final layer delivers data to
 * the active transport module.
 *
 * This is "middleware at the packet level" — composable network stacks.
 *
 * Example:
 *   pvm_pipeline_init();
 *   pvm_pipeline_push("xor_encrypt");
 *   pvm_pipeline_push("rle_compress");
 *   pvm_pipeline_send(data, len);    // encrypt → compress → transport
 *   pvm_pipeline_receive(buf, sz);   // transport → decompress → decrypt
 */
#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdint.h>
#include <stddef.h>

/** Maximum number of layers in a pipeline. */
#define PIPELINE_MAX_LAYERS  16

/** Maximum buffer size for layer transformations. */
#define PIPELINE_BUF_SIZE    65536u

/* -------------------------------------------------------------------------
 * Layer interface
 *
 * Each stackable layer must implement these two transform functions.
 * Layers do NOT own the buffer — they transform data in-place or into
 * the provided output buffer.
 * ---------------------------------------------------------------------- */

/**
 * PipelineLayer — a single composable transformation layer.
 */
typedef struct {
    const char *name;              /**< Human-readable layer name.          */

    /**
     * Transform data on the SEND path (application → wire).
     *
     * @param in       Input buffer.
     * @param in_len   Input length in bytes.
     * @param out      Output buffer (capacity = PIPELINE_BUF_SIZE).
     * @param out_len  Set to the output length on return.
     * @return         0 on success, -1 on failure.
     */
    int (*transform_send)(const uint8_t *in, size_t in_len,
                          uint8_t *out, size_t *out_len);

    /**
     * Transform data on the RECEIVE path (wire → application).
     *
     * @param in       Input buffer.
     * @param in_len   Input length in bytes.
     * @param out      Output buffer (capacity = PIPELINE_BUF_SIZE).
     * @param out_len  Set to the output length on return.
     * @return         0 on success, -1 on failure.
     */
    int (*transform_recv)(const uint8_t *in, size_t in_len,
                          uint8_t *out, size_t *out_len);
} PipelineLayer;

/* =========================================================================
 * Public API
 * ====================================================================== */

/**
 * pvm_pipeline_init — Initialise the pipeline engine.
 * @return  0 on success, -1 on failure.
 */
int pvm_pipeline_init(void);

/**
 * pvm_pipeline_shutdown — Release pipeline state.
 */
void pvm_pipeline_shutdown(void);

/**
 * pvm_pipeline_push — Add a named built-in layer to the top of the stack.
 *
 * Built-in layers:
 *   "xor_encrypt"  — XOR cipher (key=0x42) for demo encryption
 *   "rle_compress" — simple run-length encoding compression
 *   "base64"       — base64 encode/decode layer
 *   "checksum"     — appends / verifies a 1-byte checksum
 *
 * Layers are applied in push order on send (first pushed = outermost)
 * and in reverse order on receive.
 *
 * @param name  Built-in layer name.
 * @return      0 on success, -1 if the layer is unknown or stack is full.
 */
int pvm_pipeline_push(const char *name);

/**
 * pvm_pipeline_push_custom — Add a user-defined layer to the stack.
 *
 * @param layer  Pointer to a PipelineLayer with populated function pointers.
 *               The pointer must remain valid for the lifetime of the pipeline.
 * @return       0 on success, -1 if the stack is full.
 */
int pvm_pipeline_push_custom(const PipelineLayer *layer);

/**
 * pvm_pipeline_pop — Remove the most recently pushed layer from the stack.
 */
void pvm_pipeline_pop(void);

/**
 * pvm_pipeline_clear — Remove all layers.
 */
void pvm_pipeline_clear(void);

/**
 * pvm_pipeline_send — Send data through the full pipeline then via PVM.
 *
 * Applies each layer's transform_send() in stack order (first pushed
 * is applied first), then calls pvm_send() with the final result.
 *
 * @param data  Input payload.
 * @param len   Payload length.
 * @return      Bytes sent, or -1 on error.
 */
int pvm_pipeline_send(const uint8_t *data, size_t len);

/**
 * pvm_pipeline_receive — Receive data via PVM then unwind the pipeline.
 *
 * Calls pvm_receive() then applies each layer's transform_recv() in
 * reverse stack order (last pushed is unwound first).
 *
 * @param buffer   Output buffer.
 * @param max_len  Buffer capacity.
 * @return         Bytes of final payload, or -1 on error.
 */
int pvm_pipeline_receive(uint8_t *buffer, size_t max_len);

/**
 * pvm_pipeline_list — Print the current layer stack to stdout.
 */
void pvm_pipeline_list(void);

/**
 * pvm_pipeline_depth — Return the number of layers currently stacked.
 */
int pvm_pipeline_depth(void);

#endif /* PIPELINE_H */
