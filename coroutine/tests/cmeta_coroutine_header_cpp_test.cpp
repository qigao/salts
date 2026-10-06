#include <cmeta/coroutine.h>

int main() {
  cmeta_wait_handle wait{};
  cmeta_executor *executor = cmeta_current_executor();
  int status = SALTS_EIO;
  if (executor != nullptr || cmeta_current_shard(executor) != SIZE_MAX) return 1;
  if (cmeta_yield() != SALTS_EINVAL) return 1;
  if (cmeta_wait_begin(&wait) != SALTS_EINVAL || wait.owner != 0u) return 1;
  if (cmeta_wait(wait, &status) != SALTS_EINVAL || status != 0) return 1;
  if (cmeta_wait_for(wait, 1u, &status) != SALTS_EINVAL || status != 0) return 1;
  if (cmeta_wait_abort(wait) != SALTS_EINVAL) return 1;
  if (cmeta_wait_complete(executor, wait, SALTS_OK) != SALTS_EINVAL) return 1;
  return 0;
}
