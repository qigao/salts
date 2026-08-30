#include <cflow/reactive.h>

#include <stdlib.h>
#include <string.h>

static bool callback_sink_value(void *self,
                                const cmeta_type_desc *type,
                                const void *value) {
    cflow_subscriber_callbacks *cb = (cflow_subscriber_callbacks *)self;
    return cb && cb->on_value ? cb->on_value(cb->user, type, value) : true;
}
static void callback_sink_error(void *self, const char *message) {
    cflow_subscriber_callbacks *cb = (cflow_subscriber_callbacks *)self;
    if (cb && cb->on_error) cb->on_error(cb->user, message);
}
static void callback_sink_done(void *self) {
    cflow_subscriber_callbacks *cb = (cflow_subscriber_callbacks *)self;
    if (cb && cb->on_done) cb->on_done(cb->user);
}

CMETA_IMPLEMENTS(cflow_subscriber, callback_sink, 0,
    .value = callback_sink_value,
    .error = callback_sink_error,
    .done = callback_sink_done
);

cflow_subscriber cflow_subscriber_from_callbacks(cflow_subscriber_callbacks *callbacks) {
    return callback_sink_as_cflow_subscriber(callbacks);
}


typedef struct cflow_publisher_resumable_state {
    cflow_publisher source;
} cflow_publisher_resumable_state;

static cflow_step source_resumable_resume(void *self,
                                          cflow_publish_context *ctx,
                                          void *out_value) {
    cflow_publisher_resumable_state *s = (cflow_publisher_resumable_state *)self;
    return s ? cflow_publisher_resume(&s->source, ctx, out_value)
             : (cflow_step){ CFLOW_STEP_ERROR, {0}, "source-resumable state is null" };
}
static void source_resumable_cancel(void *self) {
    cflow_publisher_resumable_state *s = (cflow_publisher_resumable_state *)self;
    if (s && cflow_publisher_valid(&s->source)) cflow_publisher_cancel(&s->source);
}
static void source_resumable_destroy(void *self) {
    cflow_publisher_resumable_state *s = (cflow_publisher_resumable_state *)self;
    if (!s) return;
    if (cflow_publisher_valid(&s->source)) cflow_publisher_destroy(&s->source);
    free(s);
}
static const cflow_resumable_ops source_resumable_ops = {
    source_resumable_resume,
    source_resumable_cancel,
    source_resumable_destroy
};

bool cflow_resumable_from_publisher(cflow_resumable *out, cflow_publisher *source) {
    if (!out || !source || !cflow_publisher_valid(source)) return false;
    cflow_publisher_resumable_state *s = calloc(1, sizeof(*s));
    if (!s) return false;
    const cmeta_type_desc *type = cflow_publisher_output_type(source);
    const char *name = cflow_publisher_name(source);
    s->source = *source;
    memset(source, 0, sizeof(*source));
    *out = (cflow_resumable){ name, type, &source_resumable_ops, s };
    return true;
}
