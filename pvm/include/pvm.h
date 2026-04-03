/**
 * pvm.h — Protocol Virtual Machine Public API
 *
 * The PVM is the central runtime that ties together the Platform
 * Abstraction Layer, the packet dispatcher, and the dynamically-loaded
 * protocol modules.  Callers interact with the network entirely through
 * this API — no OS headers or protocol-specific code is needed at the
 * application level.
 *
 * Typical usage:
 *
 *   pvm_init();
 *   pvm_load("udp");
 *   pvm_load("vesper_lite");
 *   pvm_switch("vesper_lite");
 *   pvm_connect("127.0.0.1", 9000);
 *   pvm_send(data, len);
 *   pvm_receive(buf, sizeof(buf));
 *   pvm_shutdown();
 */
#ifndef PVM_H
#define PVM_H

#include <stdint.h>
#include <stddef.h>

/** Maximum number of protocol modules that can be loaded simultaneously. */
#define PVM_MAX_MODULES  16

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/**
 * pvm_init — Initialise the PVM runtime.
 *
 * Detects the host platform, initialises the PAL, and sets up the packet
 * dispatcher.  Must be called once before any other pvm_* function.
 *
 * @return  0 on success, -1 on failure.
 */
int pvm_init(void);

/**
 * pvm_shutdown — Tear down the PVM runtime.
 *
 * Calls destroy() on every loaded module, unloads their shared libraries,
 * shuts down the dispatcher, and releases all platform resources.
 */
void pvm_shutdown(void);

/* -------------------------------------------------------------------------
 * Module management
 * ---------------------------------------------------------------------- */

/**
 * pvm_load — Dynamically load a protocol module by name.
 *
 * Searches for the shared library at:
 *   <module-dir>/<name>/<name>.so   (Linux / macOS)
 *   <module-dir>/<name>/<name>.dll  (Windows)
 *
 * The module directory defaults to "./modules" relative to the working
 * directory; override with the PVM_MODULE_PATH environment variable.
 *
 * @param name  Short protocol name, e.g. "udp" or "vesper_lite".
 * @return      0 on success, -1 on failure.
 */
int pvm_load(const char *name);

/**
 * pvm_unload — Unload a previously loaded protocol module.
 *
 * Calls the module's destroy() function then releases the shared library.
 * If the module is currently active, the active protocol is cleared.
 *
 * @param name  Name of the module to unload.
 */
void pvm_unload(const char *name);

/**
 * pvm_list_modules — Print a summary of loaded modules to stdout.
 */
void pvm_list_modules(void);

/* -------------------------------------------------------------------------
 * Protocol operations
 * ---------------------------------------------------------------------- */

/**
 * pvm_switch — Make a loaded protocol the active one.
 *
 * @param name  Name of the already-loaded module to activate.
 * @return      0 on success, -1 if the module is not loaded.
 */
int pvm_switch(const char *name);

/**
 * pvm_connect — Connect the active protocol to a remote endpoint.
 *
 * @param addr  Remote address string (dotted-decimal IP or hostname).
 * @param port  Remote port number.
 * @return      0 on success, -1 on failure.
 */
int pvm_connect(const char *addr, uint16_t port);

/**
 * pvm_send — Transmit data using the active protocol.
 *
 * The PVM prepends a 1-byte protocol-identifier header before handing
 * the frame to the PAL so the dispatcher can demultiplex on receive.
 *
 * @param data  Payload buffer.
 * @param len   Payload length in bytes.
 * @return      Bytes of payload sent (excluding the PVM header), or -1
 *              on error.
 */
int pvm_send(const uint8_t *data, size_t len);

/**
 * pvm_receive — Receive data using the active protocol.
 *
 * Polls the PAL, dispatches the frame through the internal dispatcher,
 * and returns the protocol-layer payload to the caller.
 *
 * @param buffer   Destination buffer.
 * @param max_len  Buffer capacity in bytes.
 * @return         Bytes of payload received, or -1 on error / no data.
 */
int pvm_receive(uint8_t *buffer, size_t max_len);

#endif /* PVM_H */
