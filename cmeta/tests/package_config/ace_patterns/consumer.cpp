#include <cmeta/ace_interceptor.h>
#include <cmeta/ace_synchronization.h>
#include <salts/native_io_ace_token.h>
#include <salts/thread.h>
#include <type_traits>

CMETA_INTERCEPTOR_TYPE(installed_cpp_interceptor, int, int);
NATIVE_IO_ACE_TOKEN_TYPE(installed_cpp_act, int);
CMETA_ACE_SYNCHRONIZED(installed_cpp_sync, int);

static cmeta_status installed_cpp_target(void *user, const int *request, int *response) {
    *response = *request + *static_cast<int *>(user);
    return CMETA_OK;
}
static_assert(std::is_same<decltype(&installed_cpp_target),
                           installed_cpp_interceptor_target_fn>::value,
              "installed C++17 Interceptor native signature");
static void installed_cpp_lock(void *user) {
    cmeta_mutex_lock(static_cast<cmeta_mutex_t *>(user));
}
static void installed_cpp_unlock(void *user) {
    cmeta_mutex_unlock(static_cast<cmeta_mutex_t *>(user));
}
CMETA_IMPLEMENTS(cmeta_ace_lockable, installed_cpp_mutex, 0u,
    .acquire = installed_cpp_lock, .release = installed_cpp_unlock);
static cmeta_status installed_cpp_update(int *value) { ++*value; return CMETA_OK; }
static cmeta_status installed_cpp_throw(int *value) { (void)value; throw 7; }

int main() {
    int shift = 3, request = 4, response = 0;
    const installed_cpp_interceptor chain{&shift, installed_cpp_target, nullptr, 0u};
    if (installed_cpp_interceptor_invoke(&chain, &request, &response) != CMETA_OK
        || response != 7) return 1;

    const native_io_request req{1u, 3u};
    const native_io_endpoint ep{2u, 5u};
    native_io_completion done{};
    done.request = req;
    done.endpoint = ep;
    done.kind = NATIVE_IO_COMPLETION_CANCELLED;
    done.user_data = 44u;
    installed_cpp_act token{};
    int *settled = nullptr;
    if (installed_cpp_act_bind(&token, req, ep, 44u, &request) != SALTS_OK) return 2;
    if (installed_cpp_act_settle(&token, &done, &settled) != SALTS_OK ||
        settled != &request) return 3;
    if (installed_cpp_act_settle(&token, &done, &settled) != SALTS_EALREADY ||
        settled != nullptr) return 4;

    cmeta_mutex_t mutex = nullptr;
    cmeta_mutex_init(&mutex);
    cmeta_ace_lockable policy = installed_cpp_mutex_as_cmeta_ace_lockable(&mutex);
    int value = 0;
    if (installed_cpp_sync_run(&policy, &value, installed_cpp_update) != CMETA_OK ||
        value != 1) return 5;
    bool caught = false;
    try { (void)installed_cpp_sync_run(&policy, &value, installed_cpp_throw); }
    catch (int number) { caught = number == 7; }
    int rc = caught &&
        installed_cpp_sync_run(&policy, &value, installed_cpp_update) == CMETA_OK &&
        value == 2 ? 0 : 6;
    cmeta_mutex_destroy(&mutex);
    return rc;
}
