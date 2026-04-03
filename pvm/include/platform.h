/**
 * platform.h — Platform Abstraction Layer (PAL) Interface
 *
 * Defines the unified networking interface that every OS backend must
 * implement.  The PVM core and all protocol modules interact with the
 * underlying OS *exclusively* through this vtable — no OS-specific
 * headers or API calls are allowed outside of /platform.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stddef.h>

/**
 * PlatformOps — vtable for all OS-level networking operations.
 *
 * Populate with function pointers by each platform implementation and
 * return via platform_get_ops().
 */
typedef struct {
    /**
     * Initialise the platform networking back-end.
     * Must be called once before any other ops.
     * @return  0 on success, -1 on failure.
     */
    int  (*init)(void);

    /**
     * Transmit a raw frame/datagram.
     * @param data  Pointer to the byte buffer to send.
     * @param len   Number of bytes to send.
     * @return      Bytes actually sent, or -1 on error.
     */
    int  (*send_frame)(const uint8_t *data, size_t len);

    /**
     * Receive a raw frame/datagram (blocking until data arrives or
     * an error occurs).
     * @param buffer   Destination buffer.
     * @param max_len  Capacity of the destination buffer in bytes.
     * @return         Bytes received, or -1 on error / connection closed.
     */
    int  (*recv_frame)(uint8_t *buffer, size_t max_len);

    /**
     * Non-blocking poll — check whether inbound data is waiting.
     * @return  1 if data is available, 0 if not, -1 on error.
     */
    int  (*poll)(void);

    /**
     * Tear down the platform networking back-end and release all
     * OS resources (sockets, handles, etc.).
     */
    void (*shutdown)(void);
} PlatformOps;

/**
 * platform_get_ops — Return a pointer to the current platform's ops table.
 *
 * Each OS-specific implementation file provides exactly one definition
 * of this function.  The Makefile selects the correct file at build time;
 * the PVM core calls this to obtain the active PAL without ever importing
 * an OS header.
 */
const PlatformOps *platform_get_ops(void);

#endif /* PLATFORM_H */
