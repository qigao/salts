#include <cflow/cflow.h>

#include "tinytest.h"

spec("CFlow reactive IO source") {
    it("rejects an empty configuration without mutating outputs") {
        cflow_source source = {0};
        cflow_io_source_owner owner = {0};
        cflow_io_source_config config = {0};
        cflow_io_source_stats stats = {0};

        check_equal(cflow_source_from_io_actor(
                        &source, &owner, &config), TURBO_EINVAL);
        check_false(cflow_source_valid(&source));
        check_null(owner.impl);
        check_false(stats.source_live);
    }
}
