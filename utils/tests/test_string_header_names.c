#include "tstr.h"
#include "vstr.h"
#include "tinytest.h"
#include "tstr.h"
#include "vstr.h"

#include <stddef.h>

suite("canonical string headers") {
  it("preserves owned string and borrowed view behavior") {
    static const char payload[] = {'a', '\0', 'b'};
    tstr owned = tstr_dup_len(payload, sizeof(payload));
    vstr view;

    check_not_null(owned);
    view = tstr_to_v(owned);
    check_equal(view.len, sizeof(payload));
    check_equal(view.data, payload, sizeof(payload));

    tstr_free(owned);
  }
}
