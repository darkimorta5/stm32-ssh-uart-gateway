/* SPDX-License-Identifier: MIT */
#include "gateway.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t uart[64];
    size_t uart_used;
    uint8_t screen[64];
    size_t screen_used;
} demo_ports;

static gw_io_result write_uart(void *context, const uint8_t *data,
                               size_t length, size_t *accepted)
{
    demo_ports *ports = context;
    *accepted = length > 2u ? 2u : length;
    if (*accepted > sizeof(ports->uart) - ports->uart_used) {
        *accepted = 0u;
        return GW_IO_ERROR;
    }
    memcpy(ports->uart + ports->uart_used, data, *accepted);
    ports->uart_used += *accepted;
    return GW_IO_OK;
}

static gw_io_result write_ssh(void *context, const uint8_t *data,
                              size_t length, size_t *accepted)
{
    demo_ports *ports = context;
    *accepted = length > 3u ? 3u : length;
    if (*accepted > sizeof(ports->screen) - ports->screen_used) {
        *accepted = 0u;
        return GW_IO_ERROR;
    }
    memcpy(ports->screen + ports->screen_used, data, *accepted);
    ports->screen_used += *accepted;
    return GW_IO_OK;
}

int main(void)
{
    gw_gateway gateway;
    demo_ports ports = {{0u}, 0u, {0u}, 0u};
    gw_ports callbacks = {write_uart, write_ssh, &ports};
    gw_status status;
    uint32_t session, rejected;
    unsigned poll;
    /* Synthetic operator input and target output. No network or hardware opens. */
    const uint8_t input[] = "help\r";
    const uint8_t response[] = "demo> ";

    gw_init(&gateway, callbacks);
    gw_set_usb_ready(&gateway, true);
    if (gw_open(&gateway, &session) != GW_OK) return 1;
    gw_poll(&gateway, 0u);
    if (ports.uart_used != 0u) return 1;
    puts("Host simulation only. No SSH server or USB device is opened.");
    puts("USB ready, console opened: autonomous UART bytes = 0");
    puts(gw_result_text(gw_open(&gateway, &rejected)));
    if (gw_from_ssh(&gateway, session, input, sizeof(input) - 1u) != GW_OK) return 1;
    if (gw_from_uart(&gateway, response, sizeof(response) - 1u) != GW_OK) return 1;
    for (poll = 0u; poll < 16u; poll++) gw_poll(&gateway, poll);
    if (ports.uart_used != sizeof(input) - 1u ||
        memcmp(ports.uart, input, sizeof(input) - 1u) != 0 ||
        ports.screen_used != sizeof(response) - 1u ||
        memcmp(ports.screen, response, sizeof(response) - 1u) != 0) return 1;
    gw_snapshot(&gateway, &status);
    printf("diag: USB=%s active=%u UART_RX=%" PRIu64
           " UART_TX_ACCEPTED=%" PRIu64 " SSH_TX_ACCEPTED=%" PRIu64 "\n",
           status.usb_ready ? "ready" : "offline", status.session_active ? 1u : 0u,
           status.counters.uart_rx_observed, status.counters.uart_tx_accepted,
           status.counters.ssh_tx_accepted);
    if (gw_close(&gateway, session, GW_CLOSE_CLIENT) != GW_OK) return 1;
    if (gw_open(&gateway, &session) != GW_OK) return 1;
    puts("Closed and reopened: no queued bytes from the previous owner.");
    gw_snapshot(&gateway, &status);
    if (status.pending_uart != 0u || status.pending_ssh != 0u) return 1;
    if (gw_close(&gateway, session, GW_CLOSE_CLIENT) != GW_OK) return 1;
    return 0;
}
