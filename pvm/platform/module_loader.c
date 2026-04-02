/**
 * platform/module_loader.c — Dynamic Module Loading (Platform Layer)
 *
 * Provides a uniform ModuleHandle API over:
 *   POSIX (Linux / macOS) — dlopen / dlsym / dlclose  (<dlfcn.h>)
 *   Windows               — LoadLibrary / GetProcAddress / FreeLibrary
 *
 * Conditional compilation is ONLY used inside this file (and other files
 * in /platform).  All other code uses the opaque ModuleHandle type and
 * the functions declared in module_loader.h.
 */

#include <stdio.h>
#include <string.h>
#include "module_loader.h"

/* =========================================================================
 * POSIX implementation  (Linux and macOS)
 * ====================================================================== */
#if defined(__linux__) || defined(__APPLE__)

#include <dlfcn.h>

/* Static buffer for error messages returned by module_error(). */
static char last_error[256];

ModuleHandle module_load(const char *path)
{
    /* RTLD_NOW  — resolve all symbols immediately (fail-fast).
     * RTLD_LOCAL — do NOT export symbols to subsequent dlopen calls;
     *              modules must receive the PAL pointer via init().     */
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        snprintf(last_error, sizeof(last_error), "%s", dlerror());
    }
    return handle;
}

void *module_symbol(ModuleHandle handle, const char *symbol)
{
    if (!handle) return NULL;
    dlerror(); /* Clear any pending error. */
    void *sym = dlsym(handle, symbol);
    const char *err = dlerror();
    if (err) {
        snprintf(last_error, sizeof(last_error), "%s", err);
        return NULL;
    }
    return sym;
}

void module_unload(ModuleHandle handle)
{
    if (handle) dlclose(handle);
}

const char *module_error(void)
{
    return last_error;
}

/* =========================================================================
 * Windows implementation
 * ====================================================================== */
#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static char last_error[256];

ModuleHandle module_load(const char *path)
{
    HMODULE h = LoadLibraryA(path);
    if (!h) {
        snprintf(last_error, sizeof(last_error),
                 "LoadLibrary failed: error %lu", GetLastError());
    }
    return (ModuleHandle)h;
}

void *module_symbol(ModuleHandle handle, const char *symbol)
{
    if (!handle) return NULL;
    FARPROC p = GetProcAddress((HMODULE)handle, symbol);
    if (!p) {
        snprintf(last_error, sizeof(last_error),
                 "GetProcAddress('%s') failed: error %lu",
                 symbol, GetLastError());
    }
    return (void *)p;
}

void module_unload(ModuleHandle handle)
{
    if (handle) FreeLibrary((HMODULE)handle);
}

const char *module_error(void)
{
    return last_error;
}

/* =========================================================================
 * Fallback stub — unsupported platform
 * ====================================================================== */
#else

#warning "Dynamic module loading is not supported on this platform."

static const char *unsupported = "dynamic loading not supported on this platform";

ModuleHandle module_load(const char *path)   { (void)path;           return NULL; }
void        *module_symbol(ModuleHandle h, const char *s) { (void)h; (void)s; return NULL; }
void         module_unload(ModuleHandle h)   { (void)h; }
const char  *module_error(void)              { return unsupported; }

#endif /* platform selection */
