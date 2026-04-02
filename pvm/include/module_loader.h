/**
 * module_loader.h — Dynamic Module Loading Abstraction
 *
 * Wraps dlopen/dlsym/dlclose (POSIX) and LoadLibrary/GetProcAddress/
 * FreeLibrary (Windows) behind a uniform interface.  The implementation
 * lives in platform/module_loader.c so that all conditional compilation
 * is confined to the platform layer.
 */
#ifndef MODULE_LOADER_H
#define MODULE_LOADER_H

/**
 * ModuleHandle — opaque handle to a loaded dynamic library.
 * NULL indicates an invalid / unloaded handle.
 */
typedef void *ModuleHandle;

/**
 * module_load — Open a shared library and return a handle.
 *
 * @param path  Filesystem path to the .so / .dylib / .dll.
 * @return      Valid handle on success, NULL on failure.
 *              Call module_error() for a description of the failure.
 */
ModuleHandle module_load(const char *path);

/**
 * module_symbol — Resolve a symbol (function or variable) by name.
 *
 * @param handle  Library handle obtained from module_load().
 * @param symbol  Null-terminated symbol name string.
 * @return        Pointer to the symbol, or NULL if not found.
 */
void *module_symbol(ModuleHandle handle, const char *symbol);

/**
 * module_unload — Unload a previously loaded library.
 *
 * @param handle  Library handle to close.  Passing NULL is a no-op.
 */
void module_unload(ModuleHandle handle);

/**
 * module_error — Return a human-readable description of the last error.
 *
 * The returned string is valid only until the next call to any function
 * in this module.
 *
 * @return  Static error string (never NULL).
 */
const char *module_error(void);

#endif /* MODULE_LOADER_H */
