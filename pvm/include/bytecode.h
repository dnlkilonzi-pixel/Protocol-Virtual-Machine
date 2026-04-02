/**
 * bytecode.h — Protocol Bytecode Virtual Machine
 *
 * Defines a protocol instruction set that makes protocols portable
 * scripts instead of compiled .so files.  The PVM bytecode interpreter
 * executes these instructions against a frame buffer, enabling:
 *
 *   • Protocol definitions as compact bytecode programs
 *   • Portable protocol exchange between heterogeneous devices
 *   • Runtime creation of custom protocols without recompilation
 *
 * This is "WebAssembly for Networking".
 *
 * Example bytecode program:
 *   LOAD_HEADER
 *   SET_FIELD_U8   0  0x02      // version = 0x02
 *   SET_FIELD_U8   1  0x01      // type = DATA
 *   SET_FIELD_U16  2  <len>     // length = payload size
 *   APPEND_PAYLOAD
 *   SEND_FRAME
 *
 * Example API:
 *   PvmBytecodeProgram prog;
 *   pvm_bc_program_init(&prog);
 *   pvm_bc_emit(&prog, OP_LOAD_HEADER, 0, 0);
 *   pvm_bc_emit(&prog, OP_SET_FIELD_U8, 0, 0x02);
 *   pvm_bc_emit(&prog, OP_APPEND_PAYLOAD, 0, 0);
 *   pvm_bc_emit(&prog, OP_SEND_FRAME, 0, 0);
 *   pvm_bc_execute(&prog, payload, payload_len);
 */
#ifndef BYTECODE_H
#define BYTECODE_H

#include <stdint.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Bytecode instruction opcodes
 * ---------------------------------------------------------------------- */
typedef enum {
    OP_NOP            = 0x00, /**< No operation.                            */
    OP_LOAD_HEADER    = 0x01, /**< Reset frame buffer; prepare for header.  */
    OP_SET_PROTO_ID   = 0x02, /**< Set PVM proto_id byte. arg1 = value.     */
    OP_SET_FIELD_U8   = 0x03, /**< Set 1-byte field. arg1=offset, arg2=val. */
    OP_SET_FIELD_U16  = 0x04, /**< Set 2-byte BE field. arg1=off, arg2=val. */
    OP_APPEND_PAYLOAD = 0x05, /**< Append the user payload to the frame.    */
    OP_SEND_FRAME     = 0x06, /**< Transmit the assembled frame via PAL.    */
    OP_RECV_FRAME     = 0x07, /**< Receive a frame from PAL into buffer.    */
    OP_CHECK_FIELD_U8 = 0x08, /**< Verify byte at offset. arg1=off,arg2=exp.*/
    OP_EXTRACT_PAYLOAD= 0x09, /**< Copy payload region to output buffer.    */
    OP_SET_PAYLOAD_LEN= 0x0A, /**< Write payload length (BE16) at offset.   */
    OP_PRINT_FRAME    = 0x0B, /**< Debug: print current frame as hex.       */
    OP_HALT           = 0xFF, /**< End of program.                          */
} PvmOpcode;

/* -------------------------------------------------------------------------
 * Bytecode instruction
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t  opcode;   /**< PvmOpcode value.                               */
    uint16_t arg1;     /**< First argument (meaning depends on opcode).    */
    uint16_t arg2;     /**< Second argument (meaning depends on opcode).   */
} PvmInstruction;

/** Maximum instructions per program. */
#define BC_MAX_INSTRUCTIONS  256

/** Maximum frame buffer size for the bytecode VM. */
#define BC_FRAME_BUF_SIZE    65536u

/* -------------------------------------------------------------------------
 * Bytecode program
 * ---------------------------------------------------------------------- */
typedef struct {
    PvmInstruction instructions[BC_MAX_INSTRUCTIONS];
    int            count;          /**< Number of instructions emitted.     */
    char           name[64];       /**< Optional program name.              */
} PvmBytecodeProgram;

/* -------------------------------------------------------------------------
 * Bytecode VM execution context
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t  frame[BC_FRAME_BUF_SIZE]; /**< Working frame buffer.          */
    size_t   frame_len;                /**< Current frame content length.   */
    uint8_t  output[BC_FRAME_BUF_SIZE];/**< Output buffer for receive.     */
    size_t   output_len;               /**< Output content length.         */
    int      halted;                   /**< 1 if OP_HALT reached.          */
    int      error;                    /**< Non-zero on execution error.   */
    char     error_msg[128];           /**< Human-readable error message.  */
} PvmBytecodeCtx;

/* =========================================================================
 * Public API
 * ====================================================================== */

/**
 * pvm_bc_program_init — Initialise an empty bytecode program.
 */
void pvm_bc_program_init(PvmBytecodeProgram *prog, const char *name);

/**
 * pvm_bc_emit — Append one instruction to a program.
 *
 * @param prog    Target program.
 * @param opcode  Instruction opcode.
 * @param arg1    First argument.
 * @param arg2    Second argument.
 * @return        0 on success, -1 if the program is full.
 */
int pvm_bc_emit(PvmBytecodeProgram *prog, uint8_t opcode,
                uint16_t arg1, uint16_t arg2);

/**
 * pvm_bc_execute — Execute a bytecode program for SENDING.
 *
 * Runs the program against the provided user payload.  The program
 * assembles a wire frame and sends it via the PAL.
 *
 * @param prog     The bytecode program.
 * @param payload  User payload data.
 * @param len      Payload length.
 * @param ctx      Optional execution context for inspection; may be NULL.
 * @return         Bytes sent on success, -1 on error.
 */
int pvm_bc_execute(const PvmBytecodeProgram *prog,
                   const uint8_t *payload, size_t len,
                   PvmBytecodeCtx *ctx);

/**
 * pvm_bc_execute_recv — Execute a bytecode program for RECEIVING.
 *
 * Runs the program which should contain OP_RECV_FRAME, validation,
 * and OP_EXTRACT_PAYLOAD instructions.
 *
 * @param prog    The bytecode program.
 * @param buffer  Output buffer for extracted payload.
 * @param max_len Buffer capacity.
 * @param ctx     Optional execution context for inspection; may be NULL.
 * @return        Bytes received on success, -1 on error.
 */
int pvm_bc_execute_recv(const PvmBytecodeProgram *prog,
                        uint8_t *buffer, size_t max_len,
                        PvmBytecodeCtx *ctx);

/**
 * pvm_bc_disassemble — Print a human-readable listing of a program.
 */
void pvm_bc_disassemble(const PvmBytecodeProgram *prog);

/**
 * pvm_bc_opcode_name — Return a human-readable opcode name.
 */
const char *pvm_bc_opcode_name(uint8_t opcode);

#endif /* BYTECODE_H */
