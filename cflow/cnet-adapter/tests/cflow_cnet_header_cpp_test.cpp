#include "tinytest.hpp"

#include <cflow/io_cnet_adapter.h>
#include <cflow/cnet_domain_actor.h>

#include <type_traits>

static_assert(
    std::is_standard_layout<cflow_io_cnet_receive_operation>::value,
    "CNet receive operation must remain C-compatible");
static_assert(
    std::is_standard_layout<cflow_io_cnet_session_adapter>::value,
    "CNet session adapter must remain a C-compatible handle");
static_assert(
    std::is_standard_layout<cflow_io_cnet_session_adapter_config>::value,
    "CNet session adapter config must remain C-compatible");
static_assert(
    std::is_standard_layout<cflow_io_cnet_session_adapter_stats>::value,
    "CNet session adapter stats must remain C-compatible");

static_assert(
    std::is_standard_layout<cflow_cnet_domain_delivery>::value,
    "CNet domain delivery must be a typed C-compatible trivial value");
static_assert(
    std::is_standard_layout<cflow_cnet_domain_config>::value,
    "CNet domain bridge config must be a C-compatible value");

using cflow_cnet_observer_factory =
    cnet_observer (*)(cflow_io_cnet_session_adapter *);
using cflow_cnet_actor_ops_factory = cflow_io_backend_ops (*)(void);

static_assert(
    std::is_same<decltype(&cflow_io_cnet_session_adapter_observer),
                 cflow_cnet_observer_factory>::value,
    "CNet observer factory must keep its exact C signature");
static_assert(
    std::is_same<decltype(&cflow_io_cnet_session_adapter_actor_ops),
                 cflow_cnet_actor_ops_factory>::value,
    "CNet Actor backend factory must keep its exact C signature");

suite("CFlow CNet adapter C++ header") {
  it("exposes only explicit optional adapter state") {
    cflow_io_cnet_session_adapter adapter = {};
    cflow_io_cnet_session_adapter_config config = {};
    cflow_io_cnet_session_adapter_stats stats = {};
    cflow_io_cnet_receive_operation operation = {};
    cflow_cnet_domain_bridge domain = {};
    cflow_cnet_domain_delivery delivery = {};
    cflow_cnet_domain_credit credit = {};
    cnet_observer observer = {};

    check_null(adapter.impl);
    check_null(config.client);
    check_true(config.bridge_capacity == 0u);
    check_true(stats.bridge_capacity == 0u);
    check_null(domain.impl);
    check_true(delivery.generation == 0u);
    check_true(credit.slot == 0u);
    check_null(operation.buffer);
    check_true(operation.capacity == 0u);
    observer = cflow_io_cnet_session_adapter_observer(&adapter);
    check_null(observer.on_state);
    check_null(observer.on_receive);
    check_null(observer.on_send);
    check_not_null(cflow_io_cnet_session_adapter_actor_ops().submit);
  }
}
