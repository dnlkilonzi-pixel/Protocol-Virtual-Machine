# Protocol-Virtual-Machine

A **cross-platform networking runtime** (Protocol Virtual Machine — PVM) built for **VESPER OS**.

## Overview

The PVM is a networking engine where:
- Network protocols are implemented as **dynamically-loadable plugins** (`.so` / `.dll`)
- The system can **switch active protocols at runtime**
- All OS-specific networking is isolated inside a **Platform Abstraction Layer (PAL)**

```
pvm_load("udp")        → load the UDP protocol module
pvm_load("vesper_lite")→ load the VESPER-LITE protocol module
pvm_switch("vesper_lite") → make VESPER-LITE the active protocol
pvm_connect("127.0.0.1", 9001)
pvm_send(data, len)
pvm_receive(buf, sizeof(buf))
pvm_shutdown()
```

## Project Structure

```
pvm/
├── include/                   Shared OS-agnostic headers
│   ├── platform.h             PAL vtable interface
│   ├── protocol.h             Protocol module interface
│   ├── packet.h               Packet / frame types (incl. VESPER-LITE)
│   ├── dispatcher.h           Packet dispatcher interface
│   ├── pvm.h                  PVM public API
│   └── module_loader.h        Dynamic loading abstraction
├── platform/                  OS-specific implementations
│   ├── linux/platform_linux.c Linux — UDP fallback socket (AF_INET)
│   ├── windows/platform_windows.c Windows — Winsock 2
│   ├── macos/platform_macos.c macOS — BSD sockets
│   └── module_loader.c        dlopen / LoadLibrary wrapper
├── core/
│   └── pvm.c                  PVM runtime
├── net/
│   ├── packet.c               Frame build / parse helpers
│   └── dispatcher.c           Protocol demultiplexer
├── modules/
│   ├── udp/udp_module.c       UDP protocol plugin
│   └── vesper_lite/           VESPER-LITE protocol plugin
│       └── vesper_lite_module.c
├── main.c                     CLI demo application
└── Makefile                   Auto-detects OS; builds modules + binary
```

## Build

Requires GCC (C99) and GNU Make.

```bash
cd pvm
make          # build modules (.so) and pvm_demo binary
make test     # build and run the demo
make clean    # remove build artefacts
```

## VESPER-LITE Protocol

A minimal custom protocol with a 4-byte fixed header:

| Offset | Size | Field   | Description                         |
|--------|------|---------|-------------------------------------|
| 0      | 1 B  | version | Protocol version (0x01)             |
| 1      | 1 B  | type    | Message type (DATA/CTRL/ACK/ERR)    |
| 2–3    | 2 B  | length  | Payload byte count (big-endian)     |
| 4…N    | var  | payload | User data                           |

## Architecture

```
Application
    │  pvm_send / pvm_receive
    ▼
PVM Core (pvm.c)
    │  module->send / module->receive
    ▼
Protocol Module (.so / .dll)
    │  pal->send_frame / pal->recv_frame
    ▼
Platform Abstraction Layer (platform_*.c)
    │  OS socket API  (ONLY here)
    ▼
OS Network Stack
```

The PVM core **never** calls OS networking APIs directly.  
All conditional compilation (`#ifdef __linux__` etc.) is confined to `/platform`.
