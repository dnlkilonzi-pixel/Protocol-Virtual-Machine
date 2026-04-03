/**
 * core/proto_dsl.c - Protocol Domain-Specific Language (DSL)
 *
 * Parses human-readable protocol definitions and compiles them into
 * PVM bytecode programs.  Supports two formats:
 *
 *   Block format:   "protocol name { key value; ... }"
 *   Inline format:  "name: key=value key=value ..."
 *
 * This makes the PVM programmable by humans, not just C module authors.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "proto_dsl.h"
#include "bytecode.h"

/* -------------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------- */

static int dsl_inited = 0;

/* -------------------------------------------------------------------------
 * Parsing helpers
 * ---------------------------------------------------------------------- */

static void skip_whitespace(const char **p)
{
    while (**p && isspace((unsigned char)**p)) ++(*p);
}

static int parse_hex_or_dec(const char *s)
{
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return (int)strtol(s, NULL, 16);
    return atoi(s);
}

static int token_eq(const char *a, const char *b)
{
    /* Case-insensitive comparison for DSL keywords. */
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        ++a; ++b;
    }
    return (*a == '\0' && *b == '\0');
}

static DslCompression parse_compression(const char *val)
{
    if (token_eq(val, "rle"))  return DSL_COMPRESS_RLE;
    if (token_eq(val, "none")) return DSL_COMPRESS_NONE;
    return DSL_COMPRESS_NONE;
}

static DslSecurity parse_security(const char *val)
{
    if (token_eq(val, "xor"))  return DSL_SECURITY_XOR;
    if (token_eq(val, "none")) return DSL_SECURITY_NONE;
    return DSL_SECURITY_NONE;
}

static DslReliability parse_reliability(const char *val)
{
    if (token_eq(val, "high"))   return DSL_RELIABILITY_HIGH;
    if (token_eq(val, "medium")) return DSL_RELIABILITY_MEDIUM;
    if (token_eq(val, "low"))    return DSL_RELIABILITY_LOW;
    if (token_eq(val, "none"))   return DSL_RELIABILITY_NONE;
    return DSL_RELIABILITY_NONE;
}

static DslMsgType parse_msg_type(const char *val)
{
    if (token_eq(val, "data")) return DSL_TYPE_DATA;
    if (token_eq(val, "ctrl")) return DSL_TYPE_CTRL;
    if (token_eq(val, "ack"))  return DSL_TYPE_ACK;
    return DSL_TYPE_DATA;
}

/* -------------------------------------------------------------------------
 * Apply a single key=value pair to a PvmProtoDef
 * ---------------------------------------------------------------------- */
static void apply_property(PvmProtoDef *def, const char *key, const char *val)
{
    if (token_eq(key, "proto_id")) {
        def->proto_id = (uint8_t)parse_hex_or_dec(val);
    } else if (token_eq(key, "version")) {
        def->version = (uint8_t)parse_hex_or_dec(val);
    } else if (token_eq(key, "type")) {
        def->msg_type = parse_msg_type(val);
    } else if (token_eq(key, "compression")) {
        def->compression = parse_compression(val);
    } else if (token_eq(key, "security")) {
        def->security = parse_security(val);
    } else if (token_eq(key, "reliability")) {
        def->reliability = parse_reliability(val);
    } else if (token_eq(key, "header")) {
        /* "header compression rle" shorthand */
        def->compression = parse_compression(val);
    }
    /* Unknown keys are silently ignored. */
}

/* =========================================================================
 * Public API
 * ====================================================================== */

int pvm_dsl_init(void)
{
    dsl_inited = 1;
    printf("[DSL] Initialised.\n");
    return 0;
}

void pvm_dsl_shutdown(void)
{
    dsl_inited = 0;
    printf("[DSL] Shut down.\n");
}

int pvm_dsl_parse(const char *source, PvmProtoDef *def)
{
    if (!source || !def) return -1;

    memset(def, 0, sizeof(*def));
    def->version  = 1;  /* default */
    def->msg_type = DSL_TYPE_DATA;

    const char *p = source;
    skip_whitespace(&p);

    /* Detect block format: starts with "protocol " */
    if (strncmp(p, "protocol", 8) == 0 && isspace((unsigned char)p[8])) {
        p += 8;
        skip_whitespace(&p);

        /* Read protocol name. */
        int ni = 0;
        while (*p && *p != '{' && !isspace((unsigned char)*p) &&
               ni < (int)sizeof(def->name) - 1) {
            def->name[ni++] = *p++;
        }
        def->name[ni] = '\0';

        /* Skip to opening brace. */
        while (*p && *p != '{') ++p;
        if (*p == '{') ++p;

        /* Parse key-value pairs separated by semicolons or newlines. */
        while (*p && *p != '}') {
            skip_whitespace(&p);
            if (*p == '}' || *p == '\0') break;

            /* Read key. */
            char key[DSL_MAX_VALUE_LEN] = {0};
            int ki = 0;
            while (*p && !isspace((unsigned char)*p) && *p != ';' &&
                   *p != '}' && *p != '=' && ki < DSL_MAX_VALUE_LEN - 1) {
                key[ki++] = *p++;
            }
            key[ki] = '\0';

            skip_whitespace(&p);

            /* Skip optional '=' sign. */
            if (*p == '=') ++p;
            skip_whitespace(&p);

            /* Read value. */
            char val[DSL_MAX_VALUE_LEN] = {0};
            int vi = 0;
            while (*p && *p != ';' && *p != '}' && *p != '\n' &&
                   !isspace((unsigned char)*p) && vi < DSL_MAX_VALUE_LEN - 1) {
                val[vi++] = *p++;
            }
            val[vi] = '\0';

            if (ki > 0 && vi > 0)
                apply_property(def, key, val);

            /* Skip past semicolons and whitespace. */
            while (*p && (*p == ';' || isspace((unsigned char)*p))) ++p;
        }
    }
    /* Detect inline format: "name: key=value key=value ..." */
    else {
        /* Read protocol name (up to ':'). */
        int ni = 0;
        while (*p && *p != ':' && !isspace((unsigned char)*p) &&
               ni < (int)sizeof(def->name) - 1) {
            def->name[ni++] = *p++;
        }
        def->name[ni] = '\0';

        /* Skip past ':' */
        while (*p && (*p == ':' || isspace((unsigned char)*p))) ++p;

        /* Parse space-separated key=value pairs. */
        while (*p) {
            skip_whitespace(&p);
            if (*p == '\0') break;

            char key[DSL_MAX_VALUE_LEN] = {0};
            int ki = 0;
            while (*p && *p != '=' && !isspace((unsigned char)*p) &&
                   ki < DSL_MAX_VALUE_LEN - 1) {
                key[ki++] = *p++;
            }
            key[ki] = '\0';

            char val[DSL_MAX_VALUE_LEN] = {0};
            if (*p == '=') {
                ++p;
                int vi = 0;
                while (*p && !isspace((unsigned char)*p) &&
                       vi < DSL_MAX_VALUE_LEN - 1) {
                    val[vi++] = *p++;
                }
                val[vi] = '\0';
            }

            if (ki > 0 && val[0] != '\0')
                apply_property(def, key, val);
        }
    }

    /* Compute header size: proto_id(1) + version(1) + type(1) + len(2) = 5 */
    def->header_size = 5;
    def->valid = (def->name[0] != '\0') ? 1 : 0;

    if (!def->valid) {
        fprintf(stderr, "[DSL] Parse error: no protocol name found.\n");
        return -1;
    }

    return 0;
}

int pvm_dsl_compile_send(const PvmProtoDef *def, PvmBytecodeProgram *prog)
{
    if (!def || !prog || !def->valid) return -1;

    char prog_name[80];
    snprintf(prog_name, sizeof(prog_name), "dsl_send_%s", def->name);
    pvm_bc_program_init(prog, prog_name);

    /* Build the send program:
     *   frame[0] = proto_id
     *   frame[1] = version
     *   frame[2] = msg_type
     *   frame[3-4] = payload length (BE16)
     *   frame[5...] = payload
     */
    pvm_bc_emit(prog, OP_LOAD_HEADER,     0, 0);
    pvm_bc_emit(prog, OP_SET_PROTO_ID,    def->proto_id, 0);
    pvm_bc_emit(prog, OP_SET_FIELD_U8,    1, def->version);
    pvm_bc_emit(prog, OP_SET_FIELD_U8,    2, (uint8_t)def->msg_type);
    pvm_bc_emit(prog, OP_SET_PAYLOAD_LEN, 3, 0);
    pvm_bc_emit(prog, OP_APPEND_PAYLOAD,  0, 0);
    pvm_bc_emit(prog, OP_SEND_FRAME,      0, 0);
    pvm_bc_emit(prog, OP_HALT,            0, 0);

    printf("[DSL] Compiled send program for '%s' (%d instructions)\n",
           def->name, prog->count);
    return 0;
}

int pvm_dsl_compile_recv(const PvmProtoDef *def, PvmBytecodeProgram *prog)
{
    if (!def || !prog || !def->valid) return -1;

    char prog_name[80];
    snprintf(prog_name, sizeof(prog_name), "dsl_recv_%s", def->name);
    pvm_bc_program_init(prog, prog_name);

    /* Build the recv program:
     *   Receive frame
     *   Check proto_id at offset 0
     *   Check version at offset 1
     *   Extract payload starting at offset 5
     */
    pvm_bc_emit(prog, OP_RECV_FRAME,      0, 0);
    pvm_bc_emit(prog, OP_CHECK_FIELD_U8,  0, def->proto_id);
    pvm_bc_emit(prog, OP_CHECK_FIELD_U8,  1, def->version);
    pvm_bc_emit(prog, OP_EXTRACT_PAYLOAD, (uint16_t)def->header_size, 0);
    pvm_bc_emit(prog, OP_HALT,            0, 0);

    printf("[DSL] Compiled recv program for '%s' (%d instructions)\n",
           def->name, prog->count);
    return 0;
}

void pvm_dsl_print_def(const PvmProtoDef *def)
{
    if (!def) return;
    printf("[DSL] Protocol definition '%s':\n", def->name);
    printf("  proto_id:     0x%02X\n", def->proto_id);
    printf("  version:      %d\n",     def->version);
    printf("  type:         %s\n",     def->msg_type == DSL_TYPE_DATA ? "DATA" :
                                        def->msg_type == DSL_TYPE_CTRL ? "CTRL" :
                                        def->msg_type == DSL_TYPE_ACK  ? "ACK"  : "?");
    printf("  compression:  %s\n",     pvm_dsl_compression_name(def->compression));
    printf("  security:     %s\n",     pvm_dsl_security_name(def->security));
    printf("  reliability:  %s\n",     pvm_dsl_reliability_name(def->reliability));
    printf("  header_size:  %d bytes\n", def->header_size);
    printf("  valid:        %s\n",     def->valid ? "yes" : "no");
}

const char *pvm_dsl_compression_name(DslCompression c)
{
    switch (c) {
        case DSL_COMPRESS_NONE: return "none";
        case DSL_COMPRESS_RLE:  return "rle";
        default:                return "?";
    }
}

const char *pvm_dsl_security_name(DslSecurity s)
{
    switch (s) {
        case DSL_SECURITY_NONE: return "none";
        case DSL_SECURITY_XOR:  return "xor";
        default:                return "?";
    }
}

const char *pvm_dsl_reliability_name(DslReliability r)
{
    switch (r) {
        case DSL_RELIABILITY_NONE:   return "none";
        case DSL_RELIABILITY_LOW:    return "low";
        case DSL_RELIABILITY_MEDIUM: return "medium";
        case DSL_RELIABILITY_HIGH:   return "high";
        default:                     return "?";
    }
}
