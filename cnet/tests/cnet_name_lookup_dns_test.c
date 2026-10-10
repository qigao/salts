#include <cnet/name_lookup.h>
#include <salts_idna.h>
#include <salts/error_codes.h>
#include <salts/clock.h>
#include <fmt.h>
#include <tinytest.h>
#include <string.h>

static cnet_datagram server;
static cnet_name_lookup lookup;
static cnet_name_query query;
static unsigned replies;
static int response_status;
static bool silent;
static uint16_t server_port;
static bool check_idna_question, idna_question_matches;
static void sent(void *u, cnet_datagram *d, const cnet_datagram_peer *p, size_t n, int rc, uint64_t tag) {
  (void)u; (void)d; (void)p; (void)n; (void)tag; response_status = rc;
}
static void received(void *u, cnet_datagram *d, const cnet_datagram_peer *p, const cnet_receive_view *v) {
  const unsigned char *request = v->data; unsigned char response[512]; size_t question = 12;
  (void)u;
  if (silent || v->size < 17 || v->size > sizeof(response) - 56) return;
  while (question < v->size && request[question]) {
    size_t label = request[question];
    if (label > 63 || label >= v->size - question) return;
    question += label + 1;
  }
  if (question + 5 > v->size) return;
  if (check_idna_question) {
    static const unsigned char expected[] = "\x0d" "xn--bcher-kva" "\x04" "test";
    if (question + 1 - 12 != sizeof expected ||
        memcmp(request + 12, expected, sizeof expected) != 0) idna_question_matches = false;
  }
  unsigned type = ((unsigned)request[question + 1] << 8) | request[question + 2];
  memcpy(response,request,question + 5); response[2] = 0x81; response[3] = 0x80;
  response[6] = 0; response[7] = 2; response[8] = response[9] = response[10] = response[11] = 0;
  size_t size = question + 5;
  for (unsigned i = 0; i < 2; ++i) {
    const unsigned char prefix[] = {0xc0,0x0c,0,(unsigned char)type,0,1,0,0,0,60,0,(unsigned char)(type == 1 ? 4 : 16)};
    memcpy(response + size,prefix,sizeof(prefix)); size += sizeof(prefix);
    size_t bytes = type == 1 ? 4 : 16; memset(response + size,0,bytes);
    if (type == 1) { response[size] = 127; response[size + 3] = (unsigned char)(i + 1); }
    else response[size + 15] = (unsigned char)(i + 1);
    size += bytes;
  }
  response_status = cnet_datagram_send(d,p,response,size,0);
  ++replies;
}
static void progress(void) {
  size_t events = 0;
  check_equal(cnet_datagram_poll(&server,0,&events), SALTS_OK);
  check_equal(response_status, SALTS_OK);
  check_equal(cnet_name_lookup_advance(&lookup), SALTS_OK);
}
static void init_lookup(size_t results, uint32_t timeout) {
  tstr servers = tstr_format("127.0.0.1:{}", (unsigned)server_port);
  check_true(servers != NULL);
  cnet_name_lookup_config lc; cnet_name_lookup_config_init(&lc);
  lc.query_capacity = 1; lc.results_per_query = results; lc.timeout_ms = timeout; lc.servers_csv = servers;
  int rc = cnet_name_lookup_init(&lookup,&lc); tstr_free(servers); check_equal(rc, SALTS_OK);
}
suite("CNet deterministic local DNS address streams") {
  before_each() {
    memset(&server,0,sizeof(server)); memset(&lookup,0,sizeof(lookup)); memset(&query,0,sizeof(query));
    replies = 0; response_status = 0; silent = false;
    check_idna_question = false; idna_question_matches = true;
    cnet_datagram_config dc = CNET_DATAGRAM_CONFIG_INIT;
#if defined(_WIN32)
    dc.backend = NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__) || defined(__FreeBSD__)
    dc.backend = NATIVE_IO_BACKEND_KQUEUE;
#else
    dc.backend = NATIVE_IO_BACKEND_EPOLL;
#endif
    dc.host = "127.0.0.1"; dc.send_capacity = 4; dc.request_capacity = 8; dc.completion_batch_capacity = 8;
    dc.max_datagram_bytes = dc.receive_buffer_bytes = 512; dc.observer = (cnet_datagram_observer){received,sent,NULL};
    check_equal(cnet_datagram_init(&server,&dc), SALTS_OK);
    check_equal(cnet_datagram_receive(&server,16), SALTS_OK);
    check_equal(cnet_datagram_port(&server,&server_port), SALTS_OK);
    init_lookup(4,3000);
  }
  after_each() {
    if (query.owner) check_equal(cnet_name_lookup_query_drop(&lookup,&query), SALTS_OK);
    if (lookup.impl) { check_equal(cnet_name_lookup_close(&lookup), SALTS_OK); check_equal(cnet_name_lookup_destroy(&lookup), SALTS_OK); }
    if (server.impl) { check_equal(cnet_datagram_stop(&server,5000), SALTS_OK); check_equal(cnet_datagram_destroy(&server), SALTS_OK); }
  }
  it("returns every IPv4 and IPv6 result in resolver order") {
    check_equal(cnet_name_lookup_submit(&lookup,"local.test",10,&query), SALTS_OK);
    cnet_ip_address addresses[4]; size_t count = 0; int rc = SALTS_ETIMEDOUT;
    uint64_t deadline = cmeta_monotonic_ms() + 5000;
    while (cmeta_monotonic_ms() < deadline) {
      progress(); rc = cnet_name_lookup_next(&lookup,query,&addresses[count]);
      if (rc == SALTS_OK) { ++count; if (count == 4) break; }
      else if (rc != SALTS_ETIMEDOUT) break;
    }
    check_equal(count,4u); check_equal(replies,2u);
    check_equal(cnet_name_lookup_next(&lookup,query,&addresses[0]), SALTS_EOF);
    unsigned v4 = 0, v6 = 0;
    for (size_t i = 0; i < count; ++i) {
      if (addresses[i].family == CNET_DATAGRAM_ADDRESS_IPV4) check_equal(addresses[i].address[3], ++v4);
      else check_equal(addresses[i].address[15], ++v6);
    }
    check_equal(v4,2u); check_equal(v6,2u);
  }
  it("resolves an explicitly converted Unicode domain with its ASCII wire identity") {
    char mapped[256], normalized[256], ascii[254]; uint32_t scalars[256];
    salts_idna_workspace workspace = {mapped, sizeof mapped, normalized, sizeof normalized, scalars, 256};
    vstr input = vstr_from_cstr("b\xc3\xbc" "cher.test");
    size_t ascii_size = 0;
    check_equal(cnet_name_lookup_submit(&lookup, input.data, input.len, &query), SALTS_EINVAL);
    check_equal(query.owner, (uint64_t)0);
    check_equal(salts_idna_to_ascii(input, SALTS_IDNA_UNICODE17_UTS46_35_STRICT,
        &workspace, ascii, sizeof ascii, &ascii_size), SALTS_IDNA_OK);
    check_equal(ascii, "xn--bcher-kva.test");
    check_idna_question = true;
    check_equal(cnet_name_lookup_submit(&lookup, ascii, ascii_size, &query), SALTS_OK);
    cnet_ip_address address; size_t count = 0; int rc = SALTS_ETIMEDOUT;
    uint64_t deadline = cmeta_monotonic_ms() + 5000;
    while (cmeta_monotonic_ms() < deadline) {
      progress(); rc = cnet_name_lookup_next(&lookup, query, &address);
      if (rc == SALTS_OK) ++count;
      else if (rc != SALTS_ETIMEDOUT) break;
    }
    check_equal(rc, SALTS_EOF); check_equal(count, (size_t)4);
    check_equal(replies, 2u); check_true(idna_question_matches);
  }
  it("rejects overflowing result storage without a partial successful list") {
    check_equal(cnet_name_lookup_close(&lookup), SALTS_OK); check_equal(cnet_name_lookup_destroy(&lookup), SALTS_OK);
    init_lookup(1,3000);
    check_equal(cnet_name_lookup_submit(&lookup,"local.test",10,&query), SALTS_OK);
    cnet_ip_address a; int rc = SALTS_ETIMEDOUT; uint64_t deadline = cmeta_monotonic_ms() + 5000;
    while (rc == SALTS_ETIMEDOUT && cmeta_monotonic_ms() < deadline) { progress(); rc = cnet_name_lookup_next(&lookup,query,&a); }
    check_equal(rc, SALTS_ENOBUFS);
    check_equal(cnet_name_lookup_next(&lookup,query,&a), SALTS_ENOBUFS);
  }
  it("reports a deadline before recycling real c-ares callback storage") {
    check_equal(cnet_name_lookup_close(&lookup), SALTS_OK); check_equal(cnet_name_lookup_destroy(&lookup), SALTS_OK);
    init_lookup(4,10); silent = true;
    check_equal(cnet_name_lookup_submit(&lookup,"local.test",10,&query), SALTS_OK);
    cnet_ip_address a; int rc = SALTS_ETIMEDOUT; uint64_t deadline = cmeta_monotonic_ms() + 5000;
    while (rc == SALTS_ETIMEDOUT && cmeta_monotonic_ms() < deadline) { progress(); rc = cnet_name_lookup_next(&lookup,query,&a); }
    check_equal(rc, SALTS_EAI_AGAIN);
    bool ready = false; check_equal(cnet_name_lookup_ready(&lookup,query,&ready), SALTS_OK); check_true(ready);
    check_equal(cnet_name_lookup_query_drop(&lookup,&query), SALTS_OK);
    check_equal(cnet_name_lookup_submit(&lookup,"127.0.0.1",9,&query), SALTS_ENOBUFS);
  }
  it("holds dropped in-flight capacity until the actual DNS terminal") {
    silent = true;
    check_equal(cnet_name_lookup_submit(&lookup,"local.test",10,&query), SALTS_OK);
    check_equal(cnet_name_lookup_query_drop(&lookup,&query), SALTS_OK);
    check_equal(cnet_name_lookup_submit(&lookup,"127.0.0.1",9,&query), SALTS_ENOBUFS);
    check_equal(cnet_name_lookup_close(&lookup), SALTS_OK);
  }
}
