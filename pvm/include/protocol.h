/**
 * protocol.h — Protocol Module Interface
 *
 * Every protocol plugin (UDP, VESPER-LITE, …) must implement this
 * interface and export a single entry-point symbol named by
 * MODULE_ENTRY_SYMBOL.  The PVM core uses the PlatformOps vtable
 * to drive all I/O; modules receive a pointer to that vtable through
 * their init() call so they never need OS headers themselves.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "platform.h"

/**
 * ProtocolModule — vtable + metadata for a loadable protocol plugin.
 *
 * The dynamically loaded .so/.dll exports a function of type
 * module_entry_fn (see below) that returns a pointer to one of these
 * structs.  The PVM core then drives the module through its function
 * pointers.
 */
typedef struct {
    const char *name;     /**< Human-readable identifier, e.g. "udp".       */
    const char *version;  /**< Protocol version string, e.g. "1.0".         */

    /**
     * Initialise the module.  The PVM core passes in the active PAL so
     * the module can perform I/O without calling OS APIs directly.
     * @param  ops  Pointer to the platform operations vtable.
     * @return      0 on success, -1 on failure.
     */
    int  (*init)(const PlatformOps *ops);

    /**
     * Establish a logical connection to a remote endpoint.
     * For connectionless protocols this may just record the destination.
     * @param addr  Remote address string (dotted-decimal or hostname).
     * @param port  Remote port number.
     * @return      0 on success, -1 on failure.
     */
    int  (*connect)(const char *addr, uint16_t port);

    /**
     * Transmit data using this protocol.
     * @param data  Pointer to the payload buffer.
     * @param len   Payload length in bytes.
     * @return      Bytes transmitted, or -1 on error.
     */
    int  (*send)(const uint8_t *data, size_t len);

    /**
     * Receive data using this protocol.
     * @param buffer   Destination buffer.
     * @param max_len  Buffer capacity in bytes.
     * @return         Bytes received, or -1 on error.
     */
    int  (*receive)(uint8_t *buffer, size_t max_len);

    /**
     * Close the protocol connection and release connection-level state.
     * The module remains loaded and may be re-connected.
     */
    void (*close)(void);

    /**
     * Permanently destroy the module and free all associated memory.
     * Called by the PVM core just before dlclose().
     */
    void (*destroy)(void);
} ProtocolModule;

/** Signature of the factory function each .so/.dll must export. */
typedef ProtocolModule *(*module_entry_fn)(void);

/** Symbol name that every module's factory function must use. */
#define MODULE_ENTRY_SYMBOL  "get_module"

#endif /* PROTOCOL_H */
