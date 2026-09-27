#ifndef SALTS_PLUGIN_SLOW_QUERY_FIXTURE_H
#define SALTS_PLUGIN_SLOW_QUERY_FIXTURE_H

#define PLUGIN_SLOW_QUERY_ENTERED_MARKER_A \
    "salts_plugin_slow_query_entered_a.marker"
#define PLUGIN_SLOW_QUERY_ENTERED_MARKER_B \
    "salts_plugin_slow_query_entered_b.marker"
#define PLUGIN_SLOW_QUERY_RELEASE_MARKER \
    "salts_plugin_slow_query_release.marker"

#ifndef PLUGIN_SLOW_QUERY_ENTERED_MARKER
#define PLUGIN_SLOW_QUERY_ENTERED_MARKER \
    PLUGIN_SLOW_QUERY_ENTERED_MARKER_A
#endif

#endif /* SALTS_PLUGIN_SLOW_QUERY_FIXTURE_H */
