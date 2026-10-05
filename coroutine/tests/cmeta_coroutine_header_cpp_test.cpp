#include <cmeta/coroutine.h>

int main() {
  cmeta_wait_handle wait{};
  cmeta_executor *executor = cmeta_current_executor();

  return wait.owner == 0u && executor == nullptr ? 0 : 1;
}
