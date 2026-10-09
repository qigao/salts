#include <cmeta/ace_interceptor.h>
#include <cmeta/ace_synchronization.h>
#include <salts/native_io_ace_token.h>
#include <salts/thread.h>

CMETA_INTERCEPTOR_TYPE(installed_interceptor, int, int);
NATIVE_IO_ACE_TOKEN_TYPE(installed_act, int);
CMETA_ACE_SYNCHRONIZED(installed_sync, int);

static cmeta_status installed_target(void *user, const int *request, int *response) {
    *response = *request + *(int *)user;
    return CMETA_OK;
}
CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(&installed_target, installed_interceptor_target_fn),
                    "installed C11 Interceptor must preserve native signature");
static cmeta_status installed_before(void *user, const int *request, bool *proceed) {
    (void)request;
    ++*(int *)user;
    *proceed = true;
    return CMETA_OK;
}
static void installed_after(void *user, const int *request, const int *response) {
    (void)request; (void)response;
    ++*(int *)user;
}
static void installed_lock(void *user) { cmeta_mutex_lock((cmeta_mutex_t *)user); }
static void installed_unlock(void *user) { cmeta_mutex_unlock((cmeta_mutex_t *)user); }
CMETA_IMPLEMENTS(cmeta_ace_lockable, installed_mutex, 0u,
    .acquire = installed_lock, .release = installed_unlock);
static cmeta_status installed_update(int *value) { ++*value; return CMETA_OK; }

int main(void) {
    int shift = 2, observer = 0;
    int request = 5, response = 0;
    installed_interceptor_hook hook = {&observer, installed_before, installed_after, NULL};
    installed_interceptor chain = {&shift, installed_target, &hook, 1u};
    if (installed_interceptor_invoke(&chain, &request, &response) != CMETA_OK ||
        response != 7 || observer != 2) return 1;

    native_io_request req = {1u, 2u};
    native_io_endpoint ep = {2u, 3u};
    native_io_completion done = {0};
    installed_act token = {0};
    int *settled = NULL;
    done.request = req;
    done.endpoint = ep;
    done.kind = NATIVE_IO_COMPLETION_OK;
    done.user_data = 51u;
    if (installed_act_bind(&token, req, ep, 51u, &request) != SALTS_OK) return 2;
    if (installed_act_settle(&token, &done, &settled) != SALTS_OK ||
        settled != &request) return 3;
    if (installed_act_settle(&token, &done, &settled) != SALTS_EALREADY ||
        settled != NULL) return 4;

    cmeta_mutex_t mutex = NULL;
    cmeta_mutex_init(&mutex);
    cmeta_ace_lockable policy = installed_mutex_as_cmeta_ace_lockable(&mutex);
    int value = 0;
    int status = installed_sync_run(&policy, &value, installed_update);
    cmeta_mutex_destroy(&mutex);
    return status == CMETA_OK && value == 1 ? 0 : 5;
}
