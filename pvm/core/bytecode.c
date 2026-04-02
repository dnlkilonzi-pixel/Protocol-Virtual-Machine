/**
 * core/bytecode.c - Protocol Bytecode Virtual Machine
 *
 * Interprets a compact protocol instruction set, making protocols
 * portable scripts rather than compiled shared libraries.
 *
 * The bytecode VM operates on a frame buffer and executes instructions
 * that build, send, receive, validate, and extract protocol frames.
 *
 * No OS-specific code - all I/O goes through the PAL via platform.h.
 */

#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "platform.h"
#include "packet.h"

/* -------------------------------------------------------------------------
 * External: the PAL pointer is obtained from the PVM core.
 * We declare the getter from platform.h.
 * ---------------------------------------------------------------------- */

/* =========================================================================
 * Public API
 * ====================================================================== */

void pvm_bc_program_init(PvmBytecodeProgram *prog, const char *name)
{
    if (!prog) return;
    memset(prog, 0, sizeof(*prog));
    if (name) {
        strncpy(prog->name, name, sizeof(prog->name) - 1);
        prog->name[sizeof(prog->name) - 1] = '\0';
    }
}

int pvm_bc_emit(PvmBytecodeProgram *prog, uint8_t opcode,
                uint16_t arg1, uint16_t arg2)
{
    if (!prog) return -1;
    if (prog->count >= BC_MAX_INSTRUCTIONS) {
        fprintf(stderr, "[Bytecode] Program '%s' is full (%d instructions).\n",
                prog->name, BC_MAX_INSTRUCTIONS);
        return -1;
    }
    PvmInstruction *inst = &prog->instructions[prog->count++];
    inst->opcode = opcode;
    inst->arg1   = arg1;
    inst->arg2   = arg2;
    return 0;
}

/* -------------------------------------------------------------------------
 * Execution engine
 * ---------------------------------------------------------------------- */

int pvm_bc_execute(const PvmBytecodeProgram *prog,
                   const uint8_t *payload, size_t len,
                   PvmBytecodeCtx *ctx)
{
    if (!prog) return -1;

    /* Use a local context if caller did not provide one. */
    PvmBytecodeCtx local_ctx;
    PvmBytecodeCtx *c = ctx ? ctx : &local_ctx;
    memset(c, 0, sizeof(*c));

    const PlatformOps *pal = platform_get_ops();
    if (!pal) {
        snprintf(c->error_msg, sizeof(c->error_msg), "PAL unavailable");
        c->error = 1;
        return -1;
    }

    int bytes_sent = -1;

    for (int pc = 0; pc < prog->count && !c->halted && !c->error; ++pc) {
        const PvmInstruction *inst = &prog->instructions[pc];

        switch ((PvmOpcode)inst->opcode) {

        case OP_NOP:
            break;

        case OP_LOAD_HEADER:
            /* Reset the frame buffer. */
            c->frame_len = 0;
            memset(c->frame, 0, 16); /* clear header region */
            break;

        case OP_SET_PROTO_ID:
            /* Set byte 0 of the frame to the proto_id. */
            c->frame[0] = (uint8_t)(inst->arg1 & 0xFF);
            if (c->frame_len < 1) c->frame_len = 1;
            break;

        case OP_SET_FIELD_U8:
            /* Set a single byte at the given offset. */
            if (inst->arg1 < BC_FRAME_BUF_SIZE) {
                c->frame[inst->arg1] = (uint8_t)(inst->arg2 & 0xFF);
                if (c->frame_len < (size_t)(inst->arg1 + 1))
                    c->frame_len = (size_t)(inst->arg1 + 1);
            }
            break;

        case OP_SET_FIELD_U16: {
            /* Set a big-endian uint16 at the given offset. */
            size_t off = inst->arg1;
            if (off + 2 <= BC_FRAME_BUF_SIZE) {
                c->frame[off]     = (uint8_t)((inst->arg2 >> 8) & 0xFF);
                c->frame[off + 1] = (uint8_t)(inst->arg2 & 0xFF);
                if (c->frame_len < off + 2)
                    c->frame_len = off + 2;
            }
            break;
        }

        case OP_SET_PAYLOAD_LEN: {
            /* Write the actual payload length (BE16) at the given offset. */
            size_t off = inst->arg1;
            if (off + 2 <= BC_FRAME_BUF_SIZE && payload) {
                uint16_t plen = (uint16_t)len;
                c->frame[off]     = (uint8_t)((plen >> 8) & 0xFF);
                c->frame[off + 1] = (uint8_t)(plen & 0xFF);
                if (c->frame_len < off + 2)
                    c->frame_len = off + 2;
            }
            break;
        }

        case OP_APPEND_PAYLOAD:
            /* Append the user payload after the current frame content. */
            if (payload && len > 0) {
                if (c->frame_len + len > BC_FRAME_BUF_SIZE) {
                    snprintf(c->error_msg, sizeof(c->error_msg),
                             "APPEND_PAYLOAD: frame overflow");
                    c->error = 1;
                    break;
                }
                memcpy(c->frame + c->frame_len, payload, len);
                c->frame_len += len;
            }
            break;

        case OP_SEND_FRAME:
            /* Transmit the assembled frame via the PAL. */
            bytes_sent = pal->send_frame(c->frame, c->frame_len);
            if (bytes_sent < 0) {
                snprintf(c->error_msg, sizeof(c->error_msg),
                         "SEND_FRAME: PAL send_frame failed");
                c->error = 1;
            }
            break;

        case OP_PRINT_FRAME:
            /* Debug: hex-dump the current frame buffer. */
            printf("[Bytecode] Frame (%zu bytes): ", c->frame_len);
            for (size_t i = 0; i < c->frame_len && i < 48; ++i)
                printf("%02X ", c->frame[i]);
            if (c->frame_len > 48) printf("...");
            printf("\n");
            break;

        case OP_HALT:
            c->halted = 1;
            break;

        default:
            snprintf(c->error_msg, sizeof(c->error_msg),
                     "Unknown opcode 0x%02X at pc=%d", inst->opcode, pc);
            c->error = 1;
            break;
        }
    }

    if (c->error) {
        fprintf(stderr, "[Bytecode] Execution error in '%s': %s\n",
                prog->name, c->error_msg);
        return -1;
    }

    return bytes_sent;
}

int pvm_bc_execute_recv(const PvmBytecodeProgram *prog,
                        uint8_t *buffer, size_t max_len,
                        PvmBytecodeCtx *ctx)
{
    if (!prog) return -1;

    PvmBytecodeCtx local_ctx;
    PvmBytecodeCtx *c = ctx ? ctx : &local_ctx;
    memset(c, 0, sizeof(*c));

    const PlatformOps *pal = platform_get_ops();
    if (!pal) {
        snprintf(c->error_msg, sizeof(c->error_msg), "PAL unavailable");
        c->error = 1;
        return -1;
    }

    for (int pc = 0; pc < prog->count && !c->halted && !c->error; ++pc) {
        const PvmInstruction *inst = &prog->instructions[pc];

        switch ((PvmOpcode)inst->opcode) {

        case OP_NOP:
            break;

        case OP_RECV_FRAME: {
            /* Receive a frame from the PAL into the frame buffer. */
            int n = pal->recv_frame(c->frame, BC_FRAME_BUF_SIZE);
            if (n <= 0) {
                snprintf(c->error_msg, sizeof(c->error_msg),
                         "RECV_FRAME: no data");
                c->error = 1;
            } else {
                c->frame_len = (size_t)n;
            }
            break;
        }

        case OP_CHECK_FIELD_U8: {
            /* Verify a byte at offset matches expected value. */
            size_t off = inst->arg1;
            uint8_t expected = (uint8_t)(inst->arg2 & 0xFF);
            if (off >= c->frame_len || c->frame[off] != expected) {
                snprintf(c->error_msg, sizeof(c->error_msg),
                         "CHECK_FIELD_U8 at offset %u: expected 0x%02X, got 0x%02X",
                         (unsigned)off, expected,
                         (off < c->frame_len) ? c->frame[off] : 0);
                c->error = 1;
            }
            break;
        }

        case OP_EXTRACT_PAYLOAD: {
            /* Copy from frame[arg1 ... frame_len] to output buffer.
             * arg1 = start offset of payload in frame. */
            size_t start = inst->arg1;
            if (start >= c->frame_len) {
                c->output_len = 0;
            } else {
                c->output_len = c->frame_len - start;
                if (c->output_len > BC_FRAME_BUF_SIZE)
                    c->output_len = BC_FRAME_BUF_SIZE;
                memcpy(c->output, c->frame + start, c->output_len);
            }
            break;
        }

        case OP_PRINT_FRAME:
            printf("[Bytecode:recv] Frame (%zu bytes): ", c->frame_len);
            for (size_t i = 0; i < c->frame_len && i < 48; ++i)
                printf("%02X ", c->frame[i]);
            if (c->frame_len > 48) printf("...");
            printf("\n");
            break;

        case OP_HALT:
            c->halted = 1;
            break;

        default:
            /* Skip send-path opcodes silently in recv mode. */
            break;
        }
    }

    if (c->error) {
        fprintf(stderr, "[Bytecode] Recv error in '%s': %s\n",
                prog->name, c->error_msg);
        return -1;
    }

    /* Copy extracted payload to caller's buffer. */
    size_t copy_len = (c->output_len < max_len) ? c->output_len : max_len;
    if (buffer && copy_len > 0)
        memcpy(buffer, c->output, copy_len);
    return (int)copy_len;
}

/* =========================================================================
 * Disassembler / debug utilities
 * ====================================================================== */

const char *pvm_bc_opcode_name(uint8_t opcode)
{
    switch ((PvmOpcode)opcode) {
        case OP_NOP:             return "NOP";
        case OP_LOAD_HEADER:     return "LOAD_HEADER";
        case OP_SET_PROTO_ID:    return "SET_PROTO_ID";
        case OP_SET_FIELD_U8:    return "SET_FIELD_U8";
        case OP_SET_FIELD_U16:   return "SET_FIELD_U16";
        case OP_APPEND_PAYLOAD:  return "APPEND_PAYLOAD";
        case OP_SEND_FRAME:      return "SEND_FRAME";
        case OP_RECV_FRAME:      return "RECV_FRAME";
        case OP_CHECK_FIELD_U8:  return "CHECK_FIELD_U8";
        case OP_EXTRACT_PAYLOAD: return "EXTRACT_PAYLOAD";
        case OP_SET_PAYLOAD_LEN: return "SET_PAYLOAD_LEN";
        case OP_PRINT_FRAME:     return "PRINT_FRAME";
        case OP_HALT:            return "HALT";
        default:                 return "???";
    }
}

void pvm_bc_disassemble(const PvmBytecodeProgram *prog)
{
    if (!prog) return;
    printf("[Bytecode] Disassembly of '%s' (%d instructions):\n",
           prog->name, prog->count);
    for (int i = 0; i < prog->count; ++i) {
        const PvmInstruction *inst = &prog->instructions[i];
        printf("  %3d: %-18s  arg1=0x%04X  arg2=0x%04X\n",
               i,
               pvm_bc_opcode_name(inst->opcode),
               inst->arg1, inst->arg2);
    }
}
