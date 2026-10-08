#include <cnet/websocket.h>
#include <string.h>
#include <tinytest.h>

enum { FRAME_BYTES = 8, MESSAGE_BYTES = 32, INPUT_BYTES = 64, WIRE_BYTES = 128 };
static cnet_websocket websocket;
static mem_buffer_t *output;
static unsigned char output_storage[FRAME_BYTES + CNET_WEBSOCKET_MAX_HEADER_BYTES];
static unsigned char wire[WIRE_BYTES];
static size_t wire_size;
static size_t pending_size;
static size_t writes;
static size_t terminals;
static uint64_t terminal_tag;
static size_t terminal_size;
static int terminal_status;
static int writer_status;
static bool reenter;

static int write_frame(void *user, const uint8_t *data, size_t size) {
  (void)user;
  ++writes;
  if (writer_status == SALTS_EBUSY || writer_status < SALTS_OK) return writer_status;
  check_warn(size <= sizeof(wire) - wire_size);
  if (size > sizeof(wire) - wire_size) return SALTS_ENOBUFS;
  memcpy(wire + wire_size, data, size);
  wire_size += size;
  pending_size = size;
  return writer_status;
}

static void event_received(void *user, cnet_websocket *value, const cnet_websocket_event *event) {
  (void)user;
  (void)value;
  (void)event;
}

static void send_terminal(void *user, cnet_websocket *value, uint64_t tag, size_t size,
                          int status) {
  size_t events = 99u;
  (void)user;
  ++terminals;
  terminal_tag = tag;
  terminal_size = size;
  terminal_status = status;
  check_warn(cnet_websocket_advance(value, 1u, &events) == SALTS_EBUSY);
  check_warn(events == 0u);
  check_warn(cnet_websocket_destroy(value) == SALTS_EBUSY);
  if (reenter) {
    reenter = false;
    check_warn(cnet_websocket_send_tagged(value, CNET_WEBSOCKET_MESSAGE_BINARY, NULL, 0u, 9u) ==
               SALTS_OK);
  }
}

spec("CNet tagged WebSocket message terminals") {
  before_each() {
    cnet_websocket_config config = {0};
    cnet_websocket_tagged_policy policy = {sizeof(policy), CNET_WEBSOCKET_TAGGED_SEND_VERSION, 3u,
                                           send_terminal, NULL};
    wire_size = pending_size = writes = terminals = 0u;
    terminal_status = writer_status = SALTS_OK;
    terminal_tag = terminal_size = 0u;
    reenter = false;
    output = mem_wrap_external(output_storage, sizeof(output_storage), NULL, NULL);
    check_not_null(output);
    config.size = sizeof(config);
    config.role = CNET_WEBSOCKET_SERVER;
    config.max_frame_bytes = FRAME_BYTES;
    config.max_message_bytes = MESSAGE_BYTES;
    config.max_buffered_input_bytes = INPUT_BYTES;
    config.write = write_frame;
    config.on_event = event_received;
    config.output_buffer = output;
    check_equal(cnet_websocket_init_tagged(&websocket, &config, &policy), SALTS_OK);
  }

  after_each() {
    size_t events;
    reenter = false;
    /* The fake writer has no outstanding OS borrow after this test returns. */
    (void)cnet_websocket_transport_closed(&websocket);
    (void)cnet_websocket_advance(&websocket, 1u, &events);
    check_warn(cnet_websocket_destroy(&websocket) == SALTS_OK);
    mem_buffer_release(output);
    output = NULL;
  }

  it("copies a fragmented message and defers even synchronous terminal callbacks") {
    char data[] = "abcdefg";
    const unsigned char expected[] = {0x02, 3, 'a', 'b', 'c', 0x00, 3, 'd', 'e', 'f', 0x80, 1, 'g'};
    size_t events;
    check_equal(
        cnet_websocket_send_tagged(&websocket, CNET_WEBSOCKET_MESSAGE_BINARY, data, 7u, 42u),
        SALTS_OK);
    memset(data, 'x', 7u);
    check_equal(writes, 0u);
    check_equal(terminals, 0u);
    check_equal(cnet_websocket_send_binary(&websocket, "x", 1u), SALTS_EBUSY);
    check_equal(cnet_websocket_advance(&websocket, 1u, &events), SALTS_OK);
    check_equal(writes, 1u);
    check_equal(events, 0u);
    check_equal(cnet_websocket_advance(&websocket, 2u, &events), SALTS_OK);
    check_equal(events, 1u);
    check_equal(terminals, 1u);
    check_equal(terminal_tag, UINT64_C(42));
    check_equal(terminal_size, 7u);
    check_equal(terminal_status, SALTS_OK);
    check_equal(wire_size, sizeof(expected));
    check_equal(wire, expected, sizeof(expected));
    check_equal(cnet_websocket_advance(&websocket, 1u, &events), SALTS_OK);
    check_equal(events, 0u);
  }

  it("waits for each real async terminal and never credits a control frame to the tag") {
    size_t events;
    writer_status = CNET_WEBSOCKET_WRITE_PENDING;
    check_equal(
        cnet_websocket_send_tagged(&websocket, CNET_WEBSOCKET_MESSAGE_TEXT, "hello", 5u, 1u),
        SALTS_OK);
    check_equal(cnet_websocket_advance(&websocket, 8u, &events), SALTS_OK);
    check_equal(writes, 1u);
    check_equal(cnet_websocket_advance(&websocket, 8u, &events), SALTS_OK);
    check_equal(writes, 1u);
    check_equal(terminals, 0u);
    check_equal(cnet_websocket_destroy(&websocket), SALTS_EBUSY);
    check_equal(cnet_websocket_write_complete(&websocket, pending_size, SALTS_OK), SALTS_OK);
    check_equal(cnet_websocket_send_ping(&websocket, "?", 1u), SALTS_OK);
    check_equal(cnet_websocket_write_complete(&websocket, pending_size, SALTS_OK), SALTS_OK);
    check_equal(terminals, 0u);
    check_equal(cnet_websocket_advance(&websocket, 8u, &events), SALTS_OK);
    check_equal(events, 0u);
    check_equal(cnet_websocket_write_complete(&websocket, pending_size, SALTS_OK), SALTS_OK);
    check_equal(terminals, 0u);
    check_equal(cnet_websocket_advance(&websocket, 8u, &events), SALTS_OK);
    check_equal(events, 1u);
    check_equal(terminal_status, SALTS_OK);
    check_equal(writes, 3u);
    check_equal(cnet_websocket_write_complete(&websocket, pending_size, SALTS_OK), SALTS_ENOENT);
  }

  it("retries a busy frame without duplicate admission or inline callbacks") {
    size_t events;
    writer_status = SALTS_EBUSY;
    check_equal(
        cnet_websocket_send_tagged(&websocket, CNET_WEBSOCKET_MESSAGE_BINARY, "abc", 3u, 3u),
        SALTS_OK);
    check_equal(cnet_websocket_advance(&websocket, 1u, &events), SALTS_OK);
    check_equal(
        cnet_websocket_send_tagged(&websocket, CNET_WEBSOCKET_MESSAGE_BINARY, "abc", 3u, 4u),
        SALTS_EBUSY);
    writer_status = SALTS_OK;
    check_equal(cnet_websocket_advance(&websocket, 1u, &events), SALTS_OK);
    check_equal(terminals, 1u);
    check_equal(terminal_tag, UINT64_C(3));
    check_equal(wire_size, 5u);
  }

  it("settles transport failures and rejects premature destruction") {
    size_t events;
    writer_status = CNET_WEBSOCKET_WRITE_PENDING;
    check_equal(
        cnet_websocket_send_tagged(&websocket, CNET_WEBSOCKET_MESSAGE_BINARY, "abcdef", 6u, 5u),
        SALTS_OK);
    check_equal(cnet_websocket_advance(&websocket, 1u, &events), SALTS_OK);
    check_equal(cnet_websocket_write_complete(&websocket, 0u, SALTS_EIO), SALTS_EIO);
    check_equal(cnet_websocket_destroy(&websocket), SALTS_EBUSY);
    check_equal(cnet_websocket_advance(&websocket, 1u, &events), SALTS_OK);
    check_equal(events, 1u);
    check_equal(terminal_status, SALTS_EIO);
    check_equal(writes, 1u);
  }

  it("drains an admitted tagged message before accepting graceful close") {
    size_t events;
    check_equal(
        cnet_websocket_send_tagged(&websocket, CNET_WEBSOCKET_MESSAGE_BINARY, "abcdef", 6u, 6u),
        SALTS_OK);
    check_equal(cnet_websocket_close(&websocket, CNET_WEBSOCKET_CLOSE_NORMAL, NULL, 0u),
                SALTS_EBUSY);
    check_equal(cnet_websocket_advance(&websocket, 2u, &events), SALTS_OK);
    check_equal(events, 1u);
    check_equal(terminal_status, SALTS_OK);
    check_equal(writes, 2u);
    check_equal(cnet_websocket_close(&websocket, CNET_WEBSOCKET_CLOSE_NORMAL, NULL, 0u), SALTS_OK);
  }

  it("dispatches at most one terminal when the callback admits another message") {
    size_t events;
    reenter = true;
    check_equal(cnet_websocket_send_tagged(&websocket, CNET_WEBSOCKET_MESSAGE_BINARY, NULL, 0u, 8u),
                SALTS_OK);
    check_equal(cnet_websocket_advance(&websocket, 8u, &events), SALTS_OK);
    check_equal(terminals, 1u);
    check_equal(terminal_tag, UINT64_C(8));
    check_equal(cnet_websocket_advance(&websocket, 8u, &events), SALTS_OK);
    check_equal(terminals, 2u);
    check_equal(terminal_tag, UINT64_C(9));
  }

  it("rejects invalid text and oversized input before admitting any tag") {
    const unsigned char invalid[] = {0xc0, 0x80};
    unsigned char large[MESSAGE_BYTES + 1] = {0};
    size_t events;
    check_equal(cnet_websocket_send_tagged(&websocket, CNET_WEBSOCKET_MESSAGE_TEXT, invalid,
                                           sizeof(invalid), 1u),
                SALTS_ECHARSET);
    check_equal(cnet_websocket_send_tagged(&websocket, CNET_WEBSOCKET_MESSAGE_BINARY, large,
                                           sizeof(large), 2u),
                SALTS_EMSGSIZE);
    check_equal(cnet_websocket_advance(&websocket, 1u, &events), SALTS_OK);
    check_equal(terminals, 0u);
    check_equal(writes, 0u);
  }

  it("treats a short successful frame terminal as a failed logical message") {
    size_t events;
    writer_status = CNET_WEBSOCKET_WRITE_PENDING;
    check_equal(
        cnet_websocket_send_tagged(&websocket, CNET_WEBSOCKET_MESSAGE_BINARY, "abc", 3u, 20u),
        SALTS_OK);
    check_equal(cnet_websocket_advance(&websocket, 1u, &events), SALTS_OK);
    check_equal(cnet_websocket_write_complete(&websocket, pending_size - 1u, SALTS_OK),
                SALTS_EPROTO);
    check_equal(terminals, 0u);
    check_equal(cnet_websocket_advance(&websocket, 1u, &events), SALTS_OK);
    check_equal(terminals, 1u);
    check_equal(terminal_status, SALTS_EPROTO);
    check_equal(cnet_websocket_advance(&websocket, 1u, &events), SALTS_OK);
    check_equal(events, 0u);
  }

  it("keeps an asynchronous output frozen until transport drain cancels the tag") {
    size_t events;
    unsigned char frozen[sizeof(output_storage)];
    writer_status = CNET_WEBSOCKET_WRITE_PENDING;
    check_equal(
        cnet_websocket_send_tagged(&websocket, CNET_WEBSOCKET_MESSAGE_BINARY, "abcdef", 6u, 21u),
        SALTS_OK);
    check_equal(cnet_websocket_advance(&websocket, 1u, &events), SALTS_OK);
    memcpy(frozen, output_storage, sizeof(frozen));
    check_equal(cnet_websocket_close(&websocket, CNET_WEBSOCKET_CLOSE_NORMAL, NULL, 0u),
                SALTS_EBUSY);
    check_equal(cnet_websocket_advance(&websocket, 8u, &events), SALTS_OK);
    check_equal(events, 0u);
    check_equal(output_storage, frozen, sizeof(frozen));
    check_equal(cnet_websocket_destroy(&websocket), SALTS_EBUSY);
    /* Fake transport now releases its borrow before notifying the engine. */
    check_equal(cnet_websocket_transport_closed(&websocket), SALTS_OK);
    check_equal(cnet_websocket_advance(&websocket, 1u, &events), SALTS_OK);
    check_equal(terminals, 1u);
    check_equal(terminal_status, SALTS_ECANCELED);
    check_equal(writes, 1u);
  }
}
