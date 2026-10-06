#ifndef DEMO_CMETA_STANDALONE_SCHEMA_H
#define DEMO_CMETA_STANDALONE_SCHEMA_H

#include <cmeta/meta.h>

cmeta_enum(color,
    (COLOR_RED,   "red"),
    (COLOR_GREEN, "green"),
    (COLOR_BLUE,  "blue")
);

cmeta_struct(point,
    cmeta_field(int, x)
    cmeta_field(int, y)
);

#endif
