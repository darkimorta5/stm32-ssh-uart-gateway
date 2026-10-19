# Architecture and decisions

## The boundary

The intended board has an Ethernet interface, an authenticated SSH server, a USB Host controller, and a compatible FTDI serial device. This repository implements only the forwarding and lifecycle core. It does not implement authentication, encryption, USB enumeration, FTDI configuration, or a serial terminal emulator.

`gw_ports` provides two nonblocking write callbacks. An SSH adapter submits decrypted user bytes with `gw_from_ssh`. A serial adapter submits UART payload with `gw_from_uart`. `gw_poll` forwards queued data with at most one write call per direction per invocation.

All calls belong to one event loop or task. Interrupt handlers and other tasks must hand events to that owner. The core contains no locks and callbacks must not reenter it.

## One owner

A UART console is one stream with one remote terminal state. Two independent writers can interleave logins, passwords, and commands. `gw_open` therefore grants exactly one token. A second request gets `GW_BUSY`; the adapter should send `gw_result_text` to that requester and close only that request.

Tokens change on every accepted open. Delayed input or close events carrying an old token are rejected. A token is not a credential. Authentication and the choice to allow diagnostic SSH requests remain the SSH adapter's responsibility.

The SSH integration should also restrict the server to one authenticated connection and one console channel. The core cannot enforce a connection limit before the adapter calls it. See [porting](porting.md) for diagnostic command handling.

## Bounded transfers

Each direction has a 256 byte queue. `GW_QUEUE_CAPACITY` and `GW_EOF_DRAIN_MS` are explicit demonstration settings in `include/gateway.h`. There is no heap allocation. Callback acceptance consumes only the accepted prefix; a blocked callback leaves the remainder queued.

The adapter must copy the accepted bytes or take reliable ownership before returning. It must not keep a pointer into the core's queue. If its own output is asynchronous, its completion and cancellation rules must be defined separately.

An input chunk either fits in full or is rejected in full. A queue overflow closes the current session and records both the rejected chunk and queued bytes discarded during closure. The core never drops a middle segment and continues as if the stream were intact.

## What counters mean

`uart_rx_observed` counts UART payload submitted to the core, not USB packet headers. `ssh_rx_accepted` counts input accepted into the UART queue. `uart_tx_accepted` and `ssh_tx_accepted` count callback acceptance, not physical delivery or application success.

The receive accounting is:

```text
UART observed = SSH accepted + pending SSH + idle discarded
                + UART input rejected + queued SSH discarded
SSH input accepted = UART accepted + pending UART + queued UART discarded
```

These relationships assume valid callbacks and counters that have not wrapped. `ssh_input_rejected` tracks overflow and input after SSH EOF separately from accepted input. Invalid and stale requests are API errors, not accepted transport bytes.

Read only `gw_snapshot` copies readiness, ownership, queue lengths, the last close reason, and counters. It does not send input, clear counters, or recover a device. The example formats a short diagnostic line. A real SSH diagnostic command still needs authentication and server admission control.

## Close, EOF, and errors

Client close and TCP abort release ownership immediately. Core queues are cleared and their discarded bytes counted. No logout or terminal control sequence is generated. The next session starts with empty core queues, not a reset remote shell.

SSH EOF marks the input side closed. UART output can continue for a bounded drain window. Its deadline is an explicit close reason, not proof of a completed command. An on-wire byte `0x04` is different: it is ordinary user data and passes unchanged.

USB detach, reported UART line errors, USB errors, and adapter failures terminate an active session. Device readiness is supplied by the adapter; the core does not automatically retry an error or assume USB is healthy afterward. A real adapter must revoke readiness if the underlying device is unusable.

## Boot and reconnect

There is no Linux login parser or automatic recovery conversation. Target boot messages are opaque bytes. Opening a session requires a ready serial transport, not proof that Linux has booted. The user decides when to type.

This avoids an autonomous carriage return interrupting a bootloader. It does not prevent user input from doing the same. Closing the console cannot retract bytes already sent by a previous owner or distinguish late target output by session. A full integration must cancel pending adapter work and document this serial stream limitation.
