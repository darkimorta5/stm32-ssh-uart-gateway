/* SPDX-License-Identifier: MIT */
#include "gateway.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static unsigned cases;
#define CHECK(expression) do { \
    checks++; \
    if (!(expression)) { \
        fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #expression); \
        exit(1); \
    } \
} while (0)

typedef struct {
    uint8_t uart[16384];
    uint8_t ssh[16384];
    size_t uart_used;
    size_t ssh_used;
    size_t uart_limit;
    size_t ssh_limit;
    bool block_uart;
    bool block_ssh;
    bool fail_uart;
    bool fail_ssh;
    bool overreport;
    unsigned uart_calls;
    unsigned ssh_calls;
} fake_ports;

static gw_io_result capture(fake_ports *fake, bool uart, const uint8_t *data,
                            size_t length, size_t *accepted)
{
    size_t limit = uart ? fake->uart_limit : fake->ssh_limit;
    size_t *used = uart ? &fake->uart_used : &fake->ssh_used;
    uint8_t *output = uart ? fake->uart : fake->ssh;
    bool blocked = uart ? fake->block_uart : fake->block_ssh;
    bool fail = uart ? fake->fail_uart : fake->fail_ssh;
    if (uart) fake->uart_calls++; else fake->ssh_calls++;
    if (fake->overreport) {
        *accepted = length + 1u;
        return GW_IO_OK;
    }
    if (blocked) {
        *accepted = 0u;
        return GW_IO_BLOCKED;
    }
    *accepted = length < limit ? length : limit;
    CHECK(*used + *accepted <= sizeof(fake->uart));
    memcpy(output + *used, data, *accepted);
    *used += *accepted;
    return fail ? GW_IO_ERROR : GW_IO_OK;
}

static gw_io_result uart_write(void *context, const uint8_t *data,
                               size_t length, size_t *accepted)
{
    return capture(context, true, data, length, accepted);
}

static gw_io_result ssh_write(void *context, const uint8_t *data,
                              size_t length, size_t *accepted)
{
    return capture(context, false, data, length, accepted);
}

static void setup(gw_gateway *gateway, fake_ports *fake)
{
    gw_ports ports;
    memset(fake, 0, sizeof(*fake));
    fake->uart_limit = GW_QUEUE_CAPACITY;
    fake->ssh_limit = GW_QUEUE_CAPACITY;
    ports.uart_write = uart_write;
    ports.ssh_write = ssh_write;
    ports.context = fake;
    gw_init(gateway, ports);
}

static uint32_t open_ready(gw_gateway *gateway)
{
    uint32_t token;
    gw_set_usb_ready(gateway, true);
    CHECK(gw_open(gateway, &token) == GW_OK);
    CHECK(token != 0u);
    return token;
}

static void startup_is_passive(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t token = 99u;
    unsigned i;
    setup(&gateway, &fake);
    CHECK(gw_open(&gateway, &token) == GW_NOT_READY);
    CHECK(token == 0u);
    token = open_ready(&gateway);
    for (i = 0u; i < 1000u; i++) gw_poll(&gateway, i * 100000u);
    CHECK(fake.uart_calls == 0u && fake.ssh_calls == 0u);
    CHECK(gateway.owner == token);
    CHECK(gw_close(&gateway, token, GW_CLOSE_CLIENT) == GW_OK);
    CHECK(fake.uart_used == 0u);
}

static void busy_preserves_owner(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t first, second;
    const uint8_t input[] = {'a', '\r'};
    setup(&gateway, &fake);
    first = open_ready(&gateway);
    CHECK(gw_from_ssh(&gateway, first, input, sizeof(input)) == GW_OK);
    CHECK(gw_open(&gateway, &second) == GW_BUSY);
    CHECK(second == 0u && gateway.owner == first);
    gw_poll(&gateway, 0u);
    CHECK(fake.uart_used == sizeof(input));
    CHECK(memcmp(fake.uart, input, sizeof(input)) == 0);
    CHECK(gateway.counters.busy_rejections == 1u);
    CHECK(strstr(gw_result_text(GW_BUSY), "BUSY") != NULL);
}

static void control_bytes_are_unchanged(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t token;
    const uint8_t bytes[] = {0u, '\r', '\n', 8u, 127u, 3u, 4u, 27u, 255u};
    setup(&gateway, &fake);
    token = open_ready(&gateway);
    CHECK(gw_from_ssh(&gateway, token, bytes, sizeof(bytes)) == GW_OK);
    CHECK(gw_from_uart(&gateway, bytes, sizeof(bytes)) == GW_OK);
    gw_poll(&gateway, 0u);
    CHECK(fake.uart_used == sizeof(bytes) && fake.ssh_used == sizeof(bytes));
    CHECK(memcmp(fake.uart, bytes, sizeof(bytes)) == 0);
    CHECK(memcmp(fake.ssh, bytes, sizeof(bytes)) == 0);
}

static void partial_transfers_and_wrap(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t token;
    uint8_t chunk[97];
    uint8_t expected[9700];
    size_t round, index;
    setup(&gateway, &fake);
    fake.uart_limit = 3u;
    fake.ssh_limit = 7u;
    token = open_ready(&gateway);
    for (round = 0u; round < 100u; round++) {
        for (index = 0u; index < sizeof(chunk); index++) {
            chunk[index] = (uint8_t)((round * sizeof(chunk) + index) % 251u);
        }
        memcpy(expected + round * sizeof(chunk), chunk, sizeof(chunk));
        CHECK(gw_from_ssh(&gateway, token, chunk, sizeof(chunk)) == GW_OK);
        CHECK(gw_from_uart(&gateway, chunk, sizeof(chunk)) == GW_OK);
        while (gateway.to_uart.used != 0u || gateway.to_ssh.used != 0u) {
            gw_poll(&gateway, 0u);
        }
    }
    CHECK(fake.uart_used == sizeof(expected) && fake.ssh_used == sizeof(expected));
    CHECK(memcmp(fake.uart, expected, sizeof(expected)) == 0);
    CHECK(memcmp(fake.ssh, expected, sizeof(expected)) == 0);
    CHECK(gateway.counters.uart_tx_accepted == sizeof(expected));
    CHECK(gateway.counters.ssh_tx_accepted == sizeof(expected));
}

static void blocking_retains_data(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t token;
    const uint8_t bytes[] = {1u, 2u, 3u};
    setup(&gateway, &fake);
    token = open_ready(&gateway);
    fake.block_uart = true;
    fake.block_ssh = true;
    CHECK(gw_from_ssh(&gateway, token, bytes, sizeof(bytes)) == GW_OK);
    CHECK(gw_from_uart(&gateway, bytes, sizeof(bytes)) == GW_OK);
    gw_poll(&gateway, 0u);
    CHECK(gateway.to_uart.used == sizeof(bytes));
    CHECK(gateway.to_ssh.used == sizeof(bytes));
    CHECK(fake.uart_used == 0u && fake.ssh_used == 0u);
    fake.block_uart = false;
    fake.block_ssh = false;
    gw_poll(&gateway, 1u);
    CHECK(fake.uart_used == sizeof(bytes) && fake.ssh_used == sizeof(bytes));
}

static void receive_overflow_is_explicit(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t token;
    uint8_t full[GW_QUEUE_CAPACITY] = {0u};
    setup(&gateway, &fake);
    token = open_ready(&gateway);
    CHECK(gw_from_uart(&gateway, full, sizeof(full)) == GW_OK);
    CHECK(gw_from_uart(&gateway, full, 1u) == GW_OVERFLOW);
    CHECK(gateway.owner == 0u && gateway.last_close == GW_CLOSE_RX_OVERFLOW);
    CHECK(gateway.counters.uart_rx_overflows == 1u);
    CHECK(gateway.counters.uart_input_rejected == 1u);
    CHECK(gateway.counters.queued_ssh_discarded == sizeof(full));
    CHECK(gw_from_ssh(&gateway, token, full, 1u) == GW_STALE_SESSION);
    CHECK(fake.ssh_used == 0u);
}

static void transmit_overflow_is_explicit(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t token;
    uint8_t full[GW_QUEUE_CAPACITY] = {0u};
    setup(&gateway, &fake);
    token = open_ready(&gateway);
    CHECK(gw_from_ssh(&gateway, token, full, sizeof(full)) == GW_OK);
    CHECK(gw_from_ssh(&gateway, token, full, 1u) == GW_OVERFLOW);
    CHECK(gateway.owner == 0u && gateway.last_close == GW_CLOSE_TX_OVERFLOW);
    CHECK(gateway.counters.ssh_rx_overflows == 1u);
    CHECK(gateway.counters.ssh_input_rejected == 1u);
    CHECK(gateway.counters.queued_uart_discarded == sizeof(full));
    CHECK(fake.uart_used == 0u);
}

static void close_removes_queued_data(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t old, current;
    const uint8_t bytes[] = {'o', 'l', 'd'};
    setup(&gateway, &fake);
    old = open_ready(&gateway);
    CHECK(gw_from_uart(&gateway, bytes, sizeof(bytes)) == GW_OK);
    CHECK(gw_from_ssh(&gateway, old, bytes, sizeof(bytes)) == GW_OK);
    CHECK(gw_close(&gateway, old, GW_CLOSE_CLIENT) == GW_OK);
    CHECK(gateway.counters.queued_uart_discarded == sizeof(bytes));
    CHECK(gateway.counters.queued_ssh_discarded == sizeof(bytes));
    CHECK(gw_from_uart(&gateway, bytes, sizeof(bytes)) == GW_NOT_READY);
    current = open_ready(&gateway);
    CHECK(current != old);
    CHECK(gw_close(&gateway, old, GW_CLOSE_TCP_ABORT) == GW_STALE_SESSION);
    CHECK(gw_from_ssh(&gateway, old, bytes, sizeof(bytes)) == GW_STALE_SESSION);
    gw_poll(&gateway, 0u);
    CHECK(gateway.owner == current);
    CHECK(fake.uart_used == 0u && fake.ssh_used == 0u);
    CHECK(gateway.counters.idle_rx_discarded == sizeof(bytes));
}

static void usb_disconnect_and_reconnect(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t old, current;
    const uint8_t byte = 42u;
    setup(&gateway, &fake);
    old = open_ready(&gateway);
    CHECK(gw_from_ssh(&gateway, old, &byte, 1u) == GW_OK);
    gw_set_usb_ready(&gateway, false);
    CHECK(gateway.owner == 0u && gateway.last_close == GW_CLOSE_USB_DISCONNECT);
    CHECK(gw_open(&gateway, &current) == GW_NOT_READY);
    current = open_ready(&gateway);
    CHECK(current != old);
    gw_poll(&gateway, 0u);
    CHECK(fake.uart_used == 0u);
    CHECK(gw_from_ssh(&gateway, current, &byte, 1u) == GW_OK);
    gw_poll(&gateway, 0u);
    CHECK(fake.uart_used == 1u);
}

static void errors_are_events_not_recovery_commands(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t token;
    setup(&gateway, &fake);
    token = open_ready(&gateway);
    gw_report_line_error(&gateway);
    CHECK(gateway.last_close == GW_CLOSE_LINE_ERROR && gateway.owner == 0u);
    CHECK(gateway.counters.line_errors == 1u);
    CHECK(gw_close(&gateway, token, GW_CLOSE_CLIENT) == GW_STALE_SESSION);
    token = open_ready(&gateway);
    gw_report_usb_error(&gateway);
    CHECK(gateway.last_close == GW_CLOSE_USB_ERROR && gateway.owner == 0u);
    CHECK(gateway.counters.usb_errors == 1u);
    gw_poll(&gateway, 123456u);
    CHECK(fake.uart_calls == 0u);
    CHECK(gw_close(&gateway, token, GW_CLOSE_CLIENT) == GW_STALE_SESSION);
}

static void partial_then_adapter_error(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t token;
    const uint8_t input[] = {'a', 'b', 'c', 'd', 'e'};
    setup(&gateway, &fake);
    fake.uart_limit = 2u;
    fake.fail_uart = true;
    token = open_ready(&gateway);
    CHECK(gw_from_ssh(&gateway, token, input, sizeof(input)) == GW_OK);
    CHECK(gw_from_uart(&gateway, input, sizeof(input)) == GW_OK);
    gw_poll(&gateway, 0u);
    CHECK(fake.uart_used == 2u && fake.ssh_calls == 0u);
    CHECK(gateway.counters.uart_tx_accepted == 2u);
    CHECK(gateway.counters.queued_uart_discarded == 3u);
    CHECK(gateway.counters.queued_ssh_discarded == sizeof(input));
    CHECK(gateway.counters.adapter_errors == 1u);
    CHECK(gateway.owner == 0u);
}

static void adapter_contract_violation(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t token;
    const uint8_t byte = 1u;
    setup(&gateway, &fake);
    fake.overreport = true;
    token = open_ready(&gateway);
    CHECK(gw_from_ssh(&gateway, token, &byte, 1u) == GW_OK);
    gw_poll(&gateway, 0u);
    CHECK(gateway.owner == 0u && gateway.last_close == GW_CLOSE_ADAPTER_ERROR);
    CHECK(gateway.counters.queued_uart_discarded == 1u);
    CHECK(gateway.counters.uart_tx_accepted == 0u);
}

static void eof_is_not_control_d(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t token;
    const uint8_t response[] = {'o', 'k', '\r', '\n'};
    const uint32_t start = UINT32_MAX - 100u;
    setup(&gateway, &fake);
    token = open_ready(&gateway);
    CHECK(gw_ssh_eof(&gateway, token, start) == GW_OK);
    CHECK(gw_ssh_eof(&gateway, token, start + 1u) == GW_OK);
    CHECK(gw_from_ssh(&gateway, token, response, 1u) == GW_INPUT_ENDED);
    CHECK(gw_from_uart(&gateway, response, sizeof(response)) == GW_OK);
    gw_poll(&gateway, start + GW_EOF_DRAIN_MS - 1u);
    CHECK(fake.ssh_used == sizeof(response) && fake.uart_calls == 0u);
    CHECK(gateway.owner == token);
    CHECK(gw_from_uart(&gateway, response, sizeof(response)) == GW_OK);
    gw_poll(&gateway, start + GW_EOF_DRAIN_MS);
    CHECK(gateway.owner == 0u && gateway.last_close == GW_CLOSE_EOF_DEADLINE);
    CHECK(gateway.counters.eof_deadlines == 1u);
    CHECK(gateway.counters.queued_ssh_discarded == sizeof(response));
}

static void diagnostics_do_not_mutate(void)
{
    gw_gateway gateway, before;
    fake_ports fake;
    gw_status first, second;
    uint32_t token;
    setup(&gateway, &fake);
    gw_snapshot(&gateway, &first);
    CHECK(!first.usb_ready && !first.session_active);
    token = open_ready(&gateway);
    /* Preserve padding too: structure assignment need not copy its bytes. */
    memcpy(&before, &gateway, sizeof(before));
    gw_snapshot(&gateway, &first);
    gw_snapshot(&gateway, &second);
    CHECK(memcmp(&gateway, &before, sizeof(gateway)) == 0);
    CHECK(memcmp(&first, &second, sizeof(first)) == 0);
    CHECK(first.usb_ready && first.session_active && first.owner == token);
    CHECK(fake.uart_calls == 0u && fake.ssh_calls == 0u);
}

static void boot_output_is_opaque(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t token;
    const uint8_t output[] = "Bootloader countdown\r\nKernel starting\r\ndemo login: ";
    setup(&gateway, &fake);
    token = open_ready(&gateway);
    CHECK(gw_from_uart(&gateway, output, sizeof(output) - 1u) == GW_OK);
    gw_poll(&gateway, 1000000u);
    CHECK(fake.uart_calls == 0u);
    CHECK(fake.ssh_used == sizeof(output) - 1u);
    CHECK(memcmp(fake.ssh, output, sizeof(output) - 1u) == 0);
    CHECK(gateway.owner == token);
}

static void repeated_sessions_are_isolated(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t previous = 0u;
    unsigned round;
    setup(&gateway, &fake);
    for (round = 0u; round < 200u; round++) {
        uint32_t token = open_ready(&gateway);
        const uint8_t byte = (uint8_t)round;
        CHECK(token != previous);
        CHECK(gw_from_ssh(&gateway, token, &byte, 1u) == GW_OK);
        CHECK(gw_from_uart(&gateway, &byte, 1u) == GW_OK);
        CHECK(gw_close(&gateway, token, GW_CLOSE_TCP_ABORT) == GW_OK);
        previous = token;
    }
    CHECK(gateway.counters.sessions_opened == 200u);
    CHECK(gateway.counters.queued_uart_discarded == 200u);
    CHECK(gateway.counters.queued_ssh_discarded == 200u);
    CHECK(fake.uart_used == 0u && fake.ssh_used == 0u);
}

static void invalid_arguments_and_token_exhaustion(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t token;
    setup(&gateway, &fake);
    CHECK(gw_open(&gateway, NULL) == GW_INVALID);
    token = open_ready(&gateway);
    CHECK(gw_from_ssh(&gateway, token, NULL, 1u) == GW_INVALID);
    CHECK(gw_from_uart(&gateway, NULL, 1u) == GW_INVALID);
    CHECK(gw_from_ssh(&gateway, token, NULL, 0u) == GW_OK);
    CHECK(gw_from_uart(&gateway, NULL, 0u) == GW_OK);
    CHECK(gw_close(&gateway, token, GW_CLOSE_NONE) == GW_INVALID);
    CHECK(gateway.owner == token);
    CHECK(gw_close(&gateway, token, GW_CLOSE_CLIENT) == GW_OK);
    gateway.next_token = UINT32_MAX;
    CHECK(gw_open(&gateway, &token) == GW_OK && token == UINT32_MAX);
    CHECK(gw_close(&gateway, token, GW_CLOSE_CLIENT) == GW_OK);
    CHECK(gw_open(&gateway, &token) == GW_IDS_EXHAUSTED);
    CHECK(token == 0u);
    gateway.ports.ssh_write = NULL;
    CHECK(gw_open(&gateway, &token) == GW_INVALID);
}

static void zero_progress_does_not_spin(void)
{
    gw_gateway gateway;
    fake_ports fake;
    uint32_t token;
    const uint8_t byte = 1u;
    setup(&gateway, &fake);
    fake.uart_limit = 0u;
    token = open_ready(&gateway);
    CHECK(gw_from_ssh(&gateway, token, &byte, 1u) == GW_OK);
    gw_poll(&gateway, 0u);
    CHECK(fake.uart_calls == 1u && gateway.to_uart.used == 1u);
    CHECK(gateway.owner == token);
}

static void ssh_adapter_error(void)
{
    gw_gateway gateway;
    fake_ports fake;
    const uint8_t response[] = {1u, 2u, 3u};
    setup(&gateway, &fake);
    (void)open_ready(&gateway);
    fake.ssh_limit = 1u;
    fake.fail_ssh = true;
    CHECK(gw_from_uart(&gateway, response, sizeof(response)) == GW_OK);
    gw_poll(&gateway, 0u);
    CHECK(gateway.owner == 0u && gateway.last_close == GW_CLOSE_ADAPTER_ERROR);
    CHECK(gateway.counters.ssh_tx_accepted == 1u);
    CHECK(gateway.counters.queued_ssh_discarded == 2u);
}

static void run(void (*test)(void), const char *name)
{
    test();
    cases++;
    printf("PASS %s\n", name);
}

#define RUN(test) run(test, #test)
int main(void)
{
    RUN(startup_is_passive);
    RUN(busy_preserves_owner);
    RUN(control_bytes_are_unchanged);
    RUN(partial_transfers_and_wrap);
    RUN(blocking_retains_data);
    RUN(receive_overflow_is_explicit);
    RUN(transmit_overflow_is_explicit);
    RUN(close_removes_queued_data);
    RUN(usb_disconnect_and_reconnect);
    RUN(errors_are_events_not_recovery_commands);
    RUN(partial_then_adapter_error);
    RUN(adapter_contract_violation);
    RUN(eof_is_not_control_d);
    RUN(diagnostics_do_not_mutate);
    RUN(boot_output_is_opaque);
    RUN(repeated_sessions_are_isolated);
    RUN(invalid_arguments_and_token_exhaustion);
    RUN(zero_progress_does_not_spin);
    RUN(ssh_adapter_error);
    printf("RESULT cases=%u checks=%u failures=0\n", cases, checks);
    printf("CORE_SIZE bytes=%zu\n", sizeof(gw_gateway));
    return 0;
}
