#include "tinytest.hpp"

#include <cflow/io_cnet_adapter.h>
#include <cflow/cnet_domain_actor.h>
#include <cflow/cnet_domain_route.h>

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

static_assert(
    std::is_standard_layout<cflow_cnet_domain_route_credit>::value,
    "Cross-owner CNet credit must remain C-compatible");
static_assert(
    std::is_trivially_copyable<cflow_cnet_domain_route_delivery>::value,
    "Actor cross-owner envelope must remain a trivially copied token");
static_assert(
    std::is_standard_layout<cflow_cnet_domain_route_config>::value,
    "Route config must remain a C-compatible POD");

using cflow_cnet_route_recv = int (*)(
    cflow_cnet_domain_route *, cflow_cnet_domain_route_credit,
    const cnet_receive_view *);
static_assert(
    std::is_same<decltype(&cflow_cnet_domain_route_receive),
                 cflow_cnet_route_recv>::value,
    "Route receive ABI must keep its exact C signature");

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
    cflow_cnet_domain_route route = {};
    cflow_cnet_domain_route_credit routed_credit = {};
    cflow_cnet_domain_route_delivery routed_delivery = {};
    cnet_observer observer = {};

    check_null(adapter.impl);
    check_null(config.client);
    check_true(config.bridge_capacity == 0u);
    check_true(stats.bridge_capacity == 0u);
    check_null(domain.impl);
    check_true(delivery.generation == 0u);
    check_true(credit.slot == 0u);
    check_null(route.impl);
    check_true(routed_credit.generation == 0u);
    check_true(routed_delivery.source_owner == 0u);
    check_null(operation.buffer);
    check_true(operation.capacity == 0u);
    observer = cflow_io_cnet_session_adapter_observer(&adapter);
    check_null(observer.on_state);
    check_null(observer.on_receive);
    check_null(observer.on_send);
    check_not_null(cflow_io_cnet_session_adapter_actor_ops().submit);
  }
}
