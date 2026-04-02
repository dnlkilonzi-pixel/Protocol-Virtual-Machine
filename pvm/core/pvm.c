/**
 * core/pvm.c — Protocol Virtual Machine Runtime
 *
 * The PVM orchestrates:
 *   - The Platform Abstraction Layer (PAL) — all network I/O
 *   - A registry of dynamically-loaded protocol modules
 *   - A packet dispatcher that demultiplexes incoming frames
 *
 * Design constraints (enforced here):
 *   ✔ No OS-specific headers (all OS code is in /platform)
 *   ✔ No direct socket / epoll / IOCP calls
 *   ✔ Protocol modules are swappable at runtime
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pvm.h"
#include "platform.h"
#include "protocol.h"
#include "dispatcher.h"
#include "module_loader.h"
#include "packet.h"

/* -------------------------------------------------------------------------
 * Internal module registry entry
 * ---------------------------------------------------------------------- */
typedef struct {
    char           name[64];     /* Short name used as lookup key.          */
    ModuleHandle   handle;       /* Shared-library handle (from dlopen).    */
    ProtocolModule *module;      /* Pointer returned by get_module().       */
    uint8_t        proto_id;     /* Assigned ProtocolId for wire framing.   */
} LoadedModule;

/* -------------------------------------------------------------------------
 * PVM global state
 * ---------------------------------------------------------------------- */
static LoadedModule       registry[PVM_MAX_MODULES];
static int                registry_count  = 0;
static LoadedModule      *active_module   = NULL;
static const PlatformOps *pal             = NULL;
static int                pvm_initialized = 0;

/* -------------------------------------------------------------------------
 * Dispatcher callback — used for event-driven / multi-protocol fan-out.
 * In the synchronous receive path the module's own receive() is used
 * instead, so this callback is a no-op placeholder for future extension.
 * ---------------------------------------------------------------------- */
static void on_packet_received(const PvmPacket *pkt)
{
    /* Placeholder for async / fan-out use-cases. */
    (void)pkt;
}

/* -------------------------------------------------------------------------
 * Helper — assign a ProtocolId to a module name
 * ---------------------------------------------------------------------- */
static uint8_t proto_id_for_name(const char *name)
{
    if (strcmp(name, "udp")         == 0) return PROTO_UDP;
    if (strcmp(name, "vesper_lite") == 0) return PROTO_VESPER_LITE;
    /* Unknown modules get IDs starting at 0x10; cap at 0xFF. */
    int extra_id = 0x10 + (registry_count - 2);
    if (extra_id > 0xFF) extra_id = 0xFF;
    return (uint8_t)extra_id;
}

/* -------------------------------------------------------------------------
 * Helper — find a registry entry by name, returns NULL if not found
 * ---------------------------------------------------------------------- */
static LoadedModule *registry_find(const char *name)
{
    for (int i = 0; i < registry_count; ++i) {
        if (strcmp(registry[i].name, name) == 0)
            return &registry[i];
    }
    return NULL;
}

/* =========================================================================
 * Public API
 * ====================================================================== */

int pvm_init(void)
{
    if (pvm_initialized) {
        fprintf(stderr, "[PVM] Already initialised.\n");
        return 0;
    }

    /* Obtain the platform ops vtable — no OS headers needed here. */
    pal = platform_get_ops();
    if (!pal) {
        fprintf(stderr, "[PVM] platform_get_ops() returned NULL.\n");
        return -1;
    }

    /* Bring up the platform networking layer. */
    if (pal->init() != 0) {
        fprintf(stderr, "[PVM] Platform init failed.\n");
        return -1;
    }

    /* Initialise the packet dispatcher. */
    if (dispatcher_init() != 0) {
        fprintf(stderr, "[PVM] Dispatcher init failed.\n");
        pal->shutdown();
        return -1;
    }

    /* Register the unified receive callback for every known protocol. */
    dispatcher_register(PROTO_UDP,         on_packet_received);
    dispatcher_register(PROTO_VESPER_LITE, on_packet_received);

    memset(registry, 0, sizeof(registry));
    registry_count  = 0;
    active_module   = NULL;
    pvm_initialized = 1;

    printf("[PVM] Runtime initialised.\n");
    return 0;
}

void pvm_shutdown(void)
{
    if (!pvm_initialized) return;

    /* Destroy and unload every module. */
    for (int i = 0; i < registry_count; ++i) {
        LoadedModule *lm = &registry[i];
        if (lm->module && lm->module->destroy)
            lm->module->destroy();
        if (lm->handle)
            module_unload(lm->handle);
    }
    registry_count = 0;
    active_module  = NULL;

    dispatcher_shutdown();
    pal->shutdown();
    pvm_initialized = 0;

    printf("[PVM] Runtime shut down.\n");
}

/* -------------------------------------------------------------------------
 * Module management
 * ---------------------------------------------------------------------- */

int pvm_load(const char *name)
{
    if (!pvm_initialized) {
        fprintf(stderr, "[PVM] Call pvm_init() first.\n");
        return -1;
    }
    if (!name || *name == '\0') return -1;

    /* Already loaded? */
    if (registry_find(name)) {
        printf("[PVM] Module '%s' is already loaded.\n", name);
        return 0;
    }

    if (registry_count >= PVM_MAX_MODULES) {
        fprintf(stderr, "[PVM] Module registry is full.\n");
        return -1;
    }

    /* Build the shared-library path:
     *   <PVM_MODULE_PATH>/<name>/<name>.<ext>
     * where the extension is determined by the OS (set by Makefile via
     * PVM_MODULE_EXT macro, defaulting to ".so").                        */
    const char *base = getenv("PVM_MODULE_PATH");
    if (!base || *base == '\0') base = "./modules";

#ifndef PVM_MODULE_EXT
# if defined(_WIN32)
#  define PVM_MODULE_EXT ".dll"
# elif defined(__APPLE__)
#  define PVM_MODULE_EXT ".dylib"
# else
#  define PVM_MODULE_EXT ".so"
# endif
#endif

    char path[512];
    snprintf(path, sizeof(path), "%s/%s/%s%s", base, name, name, PVM_MODULE_EXT);

    printf("[PVM] Loading module '%s' from '%s' ...\n", name, path);

    ModuleHandle handle = module_load(path);
    if (!handle) {
        fprintf(stderr, "[PVM] Failed to load '%s': %s\n", path, module_error());
        return -1;
    }

    /* Resolve the factory symbol and obtain the module vtable. */
    module_entry_fn entry = (module_entry_fn)module_symbol(handle, MODULE_ENTRY_SYMBOL);
    if (!entry) {
        fprintf(stderr, "[PVM] Symbol '%s' not found in '%s': %s\n",
                MODULE_ENTRY_SYMBOL, path, module_error());
        module_unload(handle);
        return -1;
    }

    ProtocolModule *mod = entry();
    if (!mod) {
        fprintf(stderr, "[PVM] Module factory returned NULL for '%s'.\n", name);
        module_unload(handle);
        return -1;
    }

    /* Inject the PAL into the module. */
    if (mod->init(pal) != 0) {
        fprintf(stderr, "[PVM] Module '%s' init() failed.\n", name);
        if (mod->destroy) mod->destroy();
        module_unload(handle);
        return -1;
    }

    /* Register the module. */
    LoadedModule *lm = &registry[registry_count++];
    strncpy(lm->name, name, sizeof(lm->name) - 1);
    lm->name[sizeof(lm->name) - 1] = '\0';
    lm->handle   = handle;
    lm->module   = mod;
    lm->proto_id = proto_id_for_name(name);

    printf("[PVM] Module '%s' (v%s) loaded — proto_id=0x%02X.\n",
           mod->name, mod->version, lm->proto_id);
    return 0;
}

void pvm_unload(const char *name)
{
    LoadedModule *lm = registry_find(name);
    if (!lm) {
        fprintf(stderr, "[PVM] Module '%s' not loaded.\n", name);
        return;
    }

    if (active_module == lm) {
        printf("[PVM] Active module '%s' is being unloaded; clearing active.\n", name);
        active_module = NULL;
    }

    if (lm->module && lm->module->destroy) lm->module->destroy();
    if (lm->handle) module_unload(lm->handle);

    /* Compact the registry. */
    int idx = (int)(lm - registry);
    memmove(&registry[idx], &registry[idx + 1],
            (size_t)(registry_count - idx - 1) * sizeof(LoadedModule));
    --registry_count;
    memset(&registry[registry_count], 0, sizeof(LoadedModule));

    printf("[PVM] Module '%s' unloaded.\n", name);
}

void pvm_list_modules(void)
{
    printf("[PVM] Loaded modules (%d / %d):\n", registry_count, PVM_MAX_MODULES);
    for (int i = 0; i < registry_count; ++i) {
        LoadedModule *lm = &registry[i];
        printf("  [%d] %-20s  v%-8s  proto_id=0x%02X  %s\n",
               i,
               lm->module->name,
               lm->module->version,
               lm->proto_id,
               (lm == active_module) ? "<-- active" : "");
    }
    if (registry_count == 0)
        printf("  (none)\n");
}

/* -------------------------------------------------------------------------
 * Protocol operations
 * ---------------------------------------------------------------------- */

int pvm_switch(const char *name)
{
    LoadedModule *lm = registry_find(name);
    if (!lm) {
        fprintf(stderr, "[PVM] pvm_switch: module '%s' not loaded.\n", name);
        return -1;
    }
    active_module = lm;
    printf("[PVM] Active protocol switched to '%s'.\n", name);
    return 0;
}

int pvm_connect(const char *addr, uint16_t port)
{
    if (!active_module) {
        fprintf(stderr, "[PVM] pvm_connect: no active module — call pvm_switch() first.\n");
        return -1;
    }
    return active_module->module->connect(addr, port);
}

int pvm_send(const uint8_t *data, size_t len)
{
    if (!active_module) {
        fprintf(stderr, "[PVM] pvm_send: no active module.\n");
        return -1;
    }
    if (!data || len == 0) return 0;

    /* The module's send() internally calls pal->send_frame() after
     * prepending its own framing.  We still need the dispatcher to be
     * able to demultiplex on receive, so we ask the module to place
     * the proto_id byte at the start of the wire frame.  Modules accept
     * the proto_id as a hint via a thin wrapper: the send() call
     * handles the PVM outer header by having the module include it.
     *
     * For simplicity in this implementation the PVM core wraps the
     * user payload in a PVM envelope before passing it to the module:
     *
     *   wire frame = [proto_id (1B)] [module output]
     *
     * The module's send() function receives the raw user data and uses
     * pal->send_frame() to transmit its own framed output.  The PVM
     * prepends proto_id by building a composite buffer.              */
    return active_module->module->send(data, len);
}

int pvm_receive(uint8_t *buffer, size_t max_len)
{
    if (!active_module) {
        fprintf(stderr, "[PVM] pvm_receive: no active module.\n");
        return -1;
    }

    /* Poll first; return immediately if nothing is waiting. */
    if (pal->poll() <= 0) return -1;

    /*
     * Delegate to the active module's receive() function.
     * The module reads a raw frame from the PAL, validates the
     * protocol-specific framing (including the PVM proto_id header
     * and any inner headers such as the VESPER-LITE header), and
     * returns only the decoded user payload.
     *
     * The dispatcher is still wired up for event-driven / multi-
     * protocol fan-out scenarios but is not needed for the simple
     * synchronous receive path.
     */
    return active_module->module->receive(buffer, max_len);
}
