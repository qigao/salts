#ifndef DEMO_META_SCHEMA_H
#define DEMO_META_SCHEMA_H

#include <cmeta/meta.h>

cmeta_enum(demo_color,
    (DEMO_RED,   "red"),
    (DEMO_GREEN, "green"),
    (DEMO_BLUE,  "blue")
);

cmeta_enum(demo_http_status,
    (DEMO_HTTP_OK,        200, "ok"),
    (DEMO_HTTP_NOT_FOUND, 404, "not_found"),
    (DEMO_HTTP_ERROR,     500, "error")
);

cmeta_struct(demo_point,
    cmeta_field(int, x)
    cmeta_field(int, y)
);

#endif
