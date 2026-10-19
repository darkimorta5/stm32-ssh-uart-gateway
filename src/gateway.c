/* SPDX-License-Identifier: MIT */
#include "gateway.h"
#include <string.h>

static void queue_put(gw_queue *queue, const uint8_t *data, size_t length)
{
    size_t tail = (queue->head + queue->used) % GW_QUEUE_CAPACITY;
    size_t first = GW_QUEUE_CAPACITY - tail;
    if (first > length) first = length;
    if (length != 0u) {
        memcpy(queue->bytes + tail, data, first);
        memcpy(queue->bytes, data + first, length - first);
    }
    queue->used += length;
}

static void end_session(gw_gateway *gateway, gw_close_reason reason)
{
    gateway->counters.queued_uart_discarded += gateway->to_uart.used;
    gateway->counters.queued_ssh_discarded += gateway->to_ssh.used;
    /* Never replay an old session's input or output to a new owner. This cannot
     * retract bytes already accepted by a hardware or SSH adapter. */
    memset(&gateway->to_uart, 0, sizeof(gateway->to_uart));
    memset(&gateway->to_ssh, 0, sizeof(gateway->to_ssh));
    gateway->owner = 0u;
    gateway->input_ended = false;
    gateway->last_close = reason;
}

void gw_init(gw_gateway *gateway, gw_ports ports)
{
    memset(gateway, 0, sizeof(*gateway));
    gateway->ports = ports;
    gateway->next_token = 1u;
}

void gw_set_usb_ready(gw_gateway *gateway, bool ready)
{
    gateway->usb_ready = ready;
    if (!ready && gateway->owner != 0u) {
        end_session(gateway, GW_CLOSE_USB_DISCONNECT);
    }
}

gw_result gw_open(gw_gateway *gateway, uint32_t *token)
{
    if (token == NULL) return GW_INVALID;
    *token = 0u;
    if (gateway->owner != 0u) {
        gateway->counters.busy_rejections++;
        return GW_BUSY;
    }
    if (!gateway->usb_ready) return GW_NOT_READY;
    if (gateway->ports.uart_write == NULL || gateway->ports.ssh_write == NULL) {
        return GW_INVALID;
    }
    /* Stop on exhaustion rather than reuse a token that a delayed callback
     * might still hold. Tokens are lifecycle identifiers, not authentication. */
    if (gateway->next_token == 0u) return GW_IDS_EXHAUSTED;
    gateway->owner = gateway->next_token++;
    gateway->input_ended = false;
    gateway->counters.sessions_opened++;
    *token = gateway->owner;
    return GW_OK;
}

gw_result gw_from_ssh(gw_gateway *gateway, uint32_t token,
                      const uint8_t *data, size_t length)
{
    if (data == NULL && length != 0u) return GW_INVALID;
    if (token == 0u || token != gateway->owner) return GW_STALE_SESSION;
    if (gateway->input_ended) {
        gateway->counters.ssh_input_rejected += length;
        return GW_INPUT_ENDED;
    }
    if (length > GW_QUEUE_CAPACITY - gateway->to_uart.used) {
        gateway->counters.ssh_rx_overflows++;
        gateway->counters.ssh_input_rejected += length;
        end_session(gateway, GW_CLOSE_TX_OVERFLOW);
        return GW_OVERFLOW;
    }
    queue_put(&gateway->to_uart, data, length);
    gateway->counters.ssh_rx_accepted += length;
    return GW_OK;
}

gw_result gw_from_uart(gw_gateway *gateway, const uint8_t *data, size_t length)
{
    if (data == NULL && length != 0u) return GW_INVALID;
    gateway->counters.uart_rx_observed += length;
    if (!gateway->usb_ready || gateway->owner == 0u) {
        gateway->counters.idle_rx_discarded += length;
        return GW_NOT_READY;
    }
    if (length > GW_QUEUE_CAPACITY - gateway->to_ssh.used) {
        gateway->counters.uart_rx_overflows++;
        gateway->counters.uart_input_rejected += length;
        end_session(gateway, GW_CLOSE_RX_OVERFLOW);
        return GW_OVERFLOW;
    }
    queue_put(&gateway->to_ssh, data, length);
    return GW_OK;
}

gw_result gw_ssh_eof(gw_gateway *gateway, uint32_t token, uint32_t now_ms)
{
    if (token == 0u || token != gateway->owner) return GW_STALE_SESSION;
    if (!gateway->input_ended) {
        gateway->input_ended = true;
        gateway->eof_started_ms = now_ms;
    }
    return GW_OK;
}

gw_result gw_close(gw_gateway *gateway, uint32_t token, gw_close_reason reason)
{
    if (token == 0u || token != gateway->owner) return GW_STALE_SESSION;
    if (reason != GW_CLOSE_CLIENT && reason != GW_CLOSE_TCP_ABORT) return GW_INVALID;
    /* Closing SSH releases ownership; it does not send exit, CR, or Ctrl+D. */
    end_session(gateway, reason);
    return GW_OK;
}

void gw_report_line_error(gw_gateway *gateway)
{
    gateway->counters.line_errors++;
    if (gateway->owner != 0u) end_session(gateway, GW_CLOSE_LINE_ERROR);
}

void gw_report_usb_error(gw_gateway *gateway)
{
    gateway->counters.usb_errors++;
    if (gateway->owner != 0u) end_session(gateway, GW_CLOSE_USB_ERROR);
}

static void pump(gw_gateway *gateway, gw_queue *queue, gw_write_fn write,
                 uint64_t *accepted_total)
{
    size_t available = GW_QUEUE_CAPACITY - queue->head;
    size_t accepted = 0u;
    gw_io_result result;
    if (available > queue->used) available = queue->used;
    if (available == 0u) return;
    result = write(gateway->ports.context, queue->bytes + queue->head,
                   available, &accepted);
    if (accepted > available) {
        gateway->counters.adapter_errors++;
        end_session(gateway, GW_CLOSE_ADAPTER_ERROR);
        return;
    }
    queue->head = (queue->head + accepted) % GW_QUEUE_CAPACITY;
    queue->used -= accepted;
    *accepted_total += accepted;
    if (result == GW_IO_ERROR ||
        (result != GW_IO_OK && result != GW_IO_BLOCKED)) {
        gateway->counters.adapter_errors++;
        end_session(gateway, GW_CLOSE_ADAPTER_ERROR);
    }
}

void gw_poll(gw_gateway *gateway, uint32_t now_ms)
{
    if (gateway->owner == 0u || !gateway->usb_ready) return;
    /* SSH EOF is not an on-wire Ctrl+D. Continue output for a bounded drain
     * window. Reaching its deadline is not Linux command completion. */
    if (gateway->input_ended &&
        (uint32_t)(now_ms - gateway->eof_started_ms) >= GW_EOF_DRAIN_MS) {
        gateway->counters.eof_deadlines++;
        end_session(gateway, GW_CLOSE_EOF_DEADLINE);
        return;
    }
    pump(gateway, &gateway->to_uart, gateway->ports.uart_write,
         &gateway->counters.uart_tx_accepted);
    if (gateway->owner != 0u) {
        pump(gateway, &gateway->to_ssh, gateway->ports.ssh_write,
             &gateway->counters.ssh_tx_accepted);
    }
}

void gw_snapshot(const gw_gateway *gateway, gw_status *status)
{
    memset(status, 0, sizeof(*status));
    status->usb_ready = gateway->usb_ready;
    status->session_active = gateway->owner != 0u;
    status->input_ended = gateway->input_ended;
    status->owner = gateway->owner;
    status->pending_uart = gateway->to_uart.used;
    status->pending_ssh = gateway->to_ssh.used;
    status->last_close = gateway->last_close;
    status->counters = gateway->counters;
}

const char *gw_result_text(gw_result result)
{
    switch (result) {
    case GW_OK: return "OK";
    case GW_BUSY: return "BUSY: another session owns the UART";
    case GW_NOT_READY: return "USB transport not ready or no UART owner";
    case GW_STALE_SESSION: return "Session is no longer the UART owner";
    case GW_INPUT_ENDED: return "SSH input has ended";
    case GW_OVERFLOW: return "Buffer overflow: session closed, inspect counters";
    case GW_INVALID: return "Invalid argument or missing adapter";
    case GW_IDS_EXHAUSTED: return "Session identifiers exhausted";
    default: return "Unknown result";
    }
}

const char *gw_close_text(gw_close_reason reason)
{
    switch (reason) {
    case GW_CLOSE_NONE: return "none";
    case GW_CLOSE_CLIENT: return "client close";
    case GW_CLOSE_TCP_ABORT: return "TCP abort";
    case GW_CLOSE_USB_DISCONNECT: return "USB disconnected";
    case GW_CLOSE_RX_OVERFLOW: return "UART receive overflow";
    case GW_CLOSE_TX_OVERFLOW: return "SSH input overflow";
    case GW_CLOSE_LINE_ERROR: return "UART line error";
    case GW_CLOSE_USB_ERROR: return "USB transfer error";
    case GW_CLOSE_ADAPTER_ERROR: return "adapter failure";
    case GW_CLOSE_EOF_DEADLINE: return "EOF drain deadline, completion unknown";
    default: return "unknown";
    }
}
