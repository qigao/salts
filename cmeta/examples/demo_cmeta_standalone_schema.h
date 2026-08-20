#ifndef DEMO_CMETA_STANDALONE_SCHEMA_H
#define DEMO_CMETA_STANDALONE_SCHEMA_H

#include <cmeta/meta.h>

Enum(color,
    (COLOR_RED,   "red"),
    (COLOR_GREEN, "green"),
    (COLOR_BLUE,  "blue")
);

Struct(point,
    (int, x),
    (int, y)
);

#endif
