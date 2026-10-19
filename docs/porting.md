# Porting to an STM32 board

No board support package is bundled. A hardware build needs startup code, a linker script, clock and pin configuration, Ethernet, an IP stack, an SSH implementation, a USB Host stack, and an FTDI driver. Choose these independently and check their licenses before including them.

## Adapter sequence

Initialize `gw_gateway` with nonblocking UART and SSH callbacks. Keep USB readiness false until enumeration, device compatibility checks, interface selection, and serial configuration have completed. Do not map USB readiness to operating system readiness.

Authenticate an SSH client before granting it access to the core. Apply a single connection admission rule in the SSH server, then allow one console channel with `gw_open`. If another connection or channel requests access, return a short BUSY response and close only that request. No terminal bytes from a rejected client may reach UART.

Submit terminal input using the current token. Pass Enter, Backspace, escape sequences, and binary data unchanged. Submit only UART payload bytes from USB receive completions. Schedule `gw_poll` regularly; do not busy wait for output space.

Distinguish SSH EOF from channel close and a lost TCP connection. Forward EOF to `gw_ssh_eof`. When the core drops its owner, the adapter must close the corresponding SSH channel and report the actual reason where possible. Do not manufacture a successful remote command exit status.

Serialize transport completions, diagnostics, and connection events in the owner task. `gw_snapshot` is not a concurrent memory probe. Revoke USB readiness and close ownership on device detach. On reconnect, clear any adapter queues from the old device before accepting a new console.

## USB and FTDI checks

Use an explicit allowlist of supported VID/PID pairs. Check the device family, intended interface, alternate setting, bulk endpoint directions, endpoint count, and packet sizes. Do not bind to a unit's serial number unless the product actually requires it. Do not remove compatibility checks just to accept every USB device.

Parse FTDI status bytes according to the selected device's documentation before forwarding UART payload. Status only USB packets are not terminal text. Preserve raw status and packet lengths in adapter diagnostics when investigating errors. A line status flag alone is not an exact count of corrupt payload bytes.

`gw_report_line_error` expects an event already classified by the adapter. This repository does not implement that classification. Deciding whether a status only report should terminate a session requires device documentation and hardware tests, not a blanket ignore rule.

## Buffering and physical delivery

Size queues using measured burst size, scheduling latency, and worst case client backpressure. Track losses in the USB and UART layers as well as in this core. A callback must not acknowledge more bytes than it has copied or securely queued.

Before reusing UART ownership, cancel or finish pending writes from the old owner. Keep DMA and cache coherency rules in the board layer. The core cannot recall bytes already accepted by that layer. USB link speed is not a guarantee of UART throughput.

## Before a hardware release

Build an ARM image for the selected board and inspect its memory map. Provision a fresh SSH host key outside version control and configure authentication without built in public test credentials.

Check manual login, unchanged control characters, partial output, BUSY isolation, window close, and a new session. Then test a slow client, explicit overflow, USB removal, and both independent restarts. For a target restart, record autonomous UART TX separately from operator input. Record dropped bytes and error reasons instead of relying only on a visible prompt.

None of these hardware checks have been run on this public implementation.
