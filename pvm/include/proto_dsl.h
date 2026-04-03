/**
 * proto_dsl.h — Protocol Domain-Specific Language (DSL)
 *
 * Defines a human-readable mini-language for specifying protocols
 * that compiles down to PVM bytecode programs.  This makes the system
 * programmable by humans, not just C modules.
 *
 * DSL syntax example:
 *
 *   protocol vesper_custom {
 *       proto_id    0x03;
 *       version     2;
 *       type        DATA;
 *       compression rle;
 *       security    xor;
 *       reliability medium;
 *   }
 *
 * The DSL compiler parses this into a PvmProtoDef struct and can
 * generate a PvmBytecodeProgram for send/receive operations.
 *
 * The DSL also supports a simpler inline format:
 *   "proto_id=0x03 version=2 type=DATA compression=rle security=xor"
 */
#ifndef PROTO_DSL_H
#define PROTO_DSL_H

#include <stdint.h>
#include <stddef.h>
#include "bytecode.h"

/** Maximum properties in a protocol definition. */
#define DSL_MAX_PROPERTIES  32

/** Maximum length of a property key or value. */
#define DSL_MAX_VALUE_LEN   64

/* -------------------------------------------------------------------------
 * Compression modes
 * ---------------------------------------------------------------------- */
typedef enum {
    DSL_COMPRESS_NONE = 0,
    DSL_COMPRESS_RLE  = 1,
} DslCompression;

/* -------------------------------------------------------------------------
 * Security modes
 * ---------------------------------------------------------------------- */
typedef enum {
    DSL_SECURITY_NONE = 0,
    DSL_SECURITY_XOR  = 1,
} DslSecurity;

/* -------------------------------------------------------------------------
 * Reliability levels
 * ---------------------------------------------------------------------- */
typedef enum {
    DSL_RELIABILITY_NONE   = 0,   /**< Fire-and-forget (UDP-like).        */
    DSL_RELIABILITY_LOW    = 1,   /**< Best-effort.                       */
    DSL_RELIABILITY_MEDIUM = 2,   /**< Checksummed.                       */
    DSL_RELIABILITY_HIGH   = 3,   /**< Acknowledged + retransmit.         */
} DslReliability;

/* -------------------------------------------------------------------------
 * Message type codes (used in generated frames)
 * ---------------------------------------------------------------------- */
typedef enum {
    DSL_TYPE_DATA = 0x01,
    DSL_TYPE_CTRL = 0x02,
    DSL_TYPE_ACK  = 0x03,
} DslMsgType;

/* -------------------------------------------------------------------------
 * Protocol definition — result of parsing DSL
 * ---------------------------------------------------------------------- */
typedef struct {
    char             name[64];        /**< Protocol name.                   */
    uint8_t          proto_id;        /**< PVM protocol identifier byte.    */
    uint8_t          version;         /**< Protocol version byte.           */
    DslMsgType       msg_type;        /**< Default message type.            */
    DslCompression   compression;     /**< Compression mode.                */
    DslSecurity      security;        /**< Security mode.                   */
    DslReliability   reliability;     /**< Reliability level.               */
    int              header_size;     /**< Computed header size in bytes.    */
    int              valid;           /**< 1 = successfully parsed.         */
} PvmProtoDef;

/* =========================================================================
 * Public API
 * ====================================================================== */

/**
 * pvm_dsl_init — Initialise the DSL subsystem.
 * @return  0 on success, -1 on failure.
 */
int pvm_dsl_init(void);

/**
 * pvm_dsl_shutdown — Release DSL state.
 */
void pvm_dsl_shutdown(void);

/**
 * pvm_dsl_parse — Parse a DSL protocol definition string.
 *
 * Supports two formats:
 *   1. Block format:  "protocol name { key value; ... }"
 *   2. Inline format: "name: proto_id=0x03 version=2 ..."
 *
 * @param source  DSL source string (NUL-terminated).
 * @param def     Output: populated protocol definition.
 * @return        0 on success, -1 on parse error.
 */
int pvm_dsl_parse(const char *source, PvmProtoDef *def);

/**
 * pvm_dsl_compile_send — Compile a protocol def into a send bytecode program.
 *
 * The generated program:
 *   1. LOAD_HEADER
 *   2. SET_PROTO_ID
 *   3. SET_FIELD_U8 (version, type, etc.)
 *   4. SET_PAYLOAD_LEN
 *   5. APPEND_PAYLOAD
 *   6. SEND_FRAME
 *   7. HALT
 *
 * @param def   Parsed protocol definition.
 * @param prog  Output: bytecode program.
 * @return      0 on success, -1 on error.
 */
int pvm_dsl_compile_send(const PvmProtoDef *def, PvmBytecodeProgram *prog);

/**
 * pvm_dsl_compile_recv — Compile a protocol def into a recv bytecode program.
 *
 * The generated program:
 *   1. RECV_FRAME
 *   2. CHECK_FIELD_U8 (proto_id, version)
 *   3. EXTRACT_PAYLOAD
 *   4. HALT
 *
 * @param def   Parsed protocol definition.
 * @param prog  Output: bytecode program.
 * @return      0 on success, -1 on error.
 */
int pvm_dsl_compile_recv(const PvmProtoDef *def, PvmBytecodeProgram *prog);

/**
 * pvm_dsl_print_def — Print a parsed protocol definition to stdout.
 */
void pvm_dsl_print_def(const PvmProtoDef *def);

/**
 * pvm_dsl_compression_name — Human-readable compression name.
 */
const char *pvm_dsl_compression_name(DslCompression c);

/**
 * pvm_dsl_security_name — Human-readable security name.
 */
const char *pvm_dsl_security_name(DslSecurity s);

/**
 * pvm_dsl_reliability_name — Human-readable reliability name.
 */
const char *pvm_dsl_reliability_name(DslReliability r);

#endif /* PROTO_DSL_H */
