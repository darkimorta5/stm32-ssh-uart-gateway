/* SPDX-License-Identifier: MIT */
#ifndef GATEWAY_H
#define GATEWAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Small demonstration queues, not a production throughput recommendation. */
#define GW_QUEUE_CAPACITY 256u
#define GW_EOF_DRAIN_MS 3000u

typedef enum {
    GW_OK = 0,
    GW_BUSY,
    GW_NOT_READY,
    GW_STALE_SESSION,
    GW_INPUT_ENDED,
    GW_OVERFLOW,
    GW_INVALID,
    GW_IDS_EXHAUSTED
} gw_result;

typedef enum { GW_IO_OK = 0, GW_IO_BLOCKED, GW_IO_ERROR } gw_io_result;

typedef enum {
    GW_CLOSE_NONE = 0,
    GW_CLOSE_CLIENT,
    GW_CLOSE_TCP_ABORT,
    GW_CLOSE_USB_DISCONNECT,
    GW_CLOSE_RX_OVERFLOW,
    GW_CLOSE_TX_OVERFLOW,
    GW_CLOSE_LINE_ERROR,
    GW_CLOSE_USB_ERROR,
    GW_CLOSE_ADAPTER_ERROR,
    GW_CLOSE_EOF_DEADLINE
} gw_close_reason;

/* Nonblocking, synchronous copy. *accepted counts only confirmed ownership
 * transfers. The callback must not retain data or call back into this core.
 * An error may follow a partial acceptance; that prefix is still accounted for.
 * These counts do not prove physical UART delivery or remote command success. */
typedef gw_io_result (*gw_write_fn)(void *context, const uint8_t *data,
                                  size_t length, size_t *accepted);

typedef struct {
    gw_write_fn uart_write;
    gw_write_fn ssh_write;
    void *context;
} gw_ports;

typedef struct {
    uint64_t sessions_opened;
    uint64_t busy_rejections;
    uint64_t ssh_rx_accepted;
    uint64_t uart_rx_observed;
    uint64_t uart_tx_accepted;
    uint64_t ssh_tx_accepted;
    uint64_t idle_rx_discarded;
    uint64_t ssh_input_rejected;
    uint64_t uart_input_rejected;
    uint64_t queued_uart_discarded;
    uint64_t queued_ssh_discarded;
    uint64_t uart_rx_overflows;
    uint64_t ssh_rx_overflows;
    uint64_t line_errors;
    uint64_t usb_errors;
    uint64_t adapter_errors;
    uint64_t eof_deadlines;
} gw_counters;

typedef struct {
    uint8_t bytes[GW_QUEUE_CAPACITY];
    size_t head;
    size_t used;
} gw_queue;

/* All access belongs to one event loop or task. This is not an ISR-safe API. */
typedef struct {
    gw_ports ports;
    gw_queue to_uart;
    gw_queue to_ssh;
    gw_counters counters;
    uint32_t owner;
    uint32_t next_token;
    uint32_t eof_started_ms;
    bool usb_ready;
    bool input_ended;
    gw_close_reason last_close;
} gw_gateway;

typedef struct {
    bool usb_ready;
    bool session_active;
    bool input_ended;
    uint32_t owner;
    size_t pending_uart;
    size_t pending_ssh;
    gw_close_reason last_close;
    gw_counters counters;
} gw_status;

void gw_init(gw_gateway *gateway, gw_ports ports);
void gw_set_usb_ready(gw_gateway *gateway, bool ready);
gw_result gw_open(gw_gateway *gateway, uint32_t *token);
gw_result gw_from_ssh(gw_gateway *gateway, uint32_t token,
                      const uint8_t *data, size_t length);
gw_result gw_from_uart(gw_gateway *gateway, const uint8_t *data, size_t length);
gw_result gw_ssh_eof(gw_gateway *gateway, uint32_t token, uint32_t now_ms);
gw_result gw_close(gw_gateway *gateway, uint32_t token, gw_close_reason reason);
void gw_report_line_error(gw_gateway *gateway);
void gw_report_usb_error(gw_gateway *gateway);
void gw_poll(gw_gateway *gateway, uint32_t now_ms);
void gw_snapshot(const gw_gateway *gateway, gw_status *status);
const char *gw_result_text(gw_result result);
const char *gw_close_text(gw_close_reason reason);

#endif
