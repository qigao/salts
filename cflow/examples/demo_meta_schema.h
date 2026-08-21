#ifndef DEMO_META_SCHEMA_H
#define DEMO_META_SCHEMA_H

#include <cmeta/meta.h>

Enum(demo_color,
    (DEMO_RED,   "red"),
    (DEMO_GREEN, "green"),
    (DEMO_BLUE,  "blue")
);

Enum(demo_http_status,
    (DEMO_HTTP_OK,        200, "ok"),
    (DEMO_HTTP_NOT_FOUND, 404, "not_found"),
    (DEMO_HTTP_ERROR,     500, "error")
);

Struct(demo_point,
    (int, x),
    (int, y)
);

#endif
