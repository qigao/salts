#include <cflow/runtime.h>

#include <stdlib.h>
#include <string.h>

static bool callback_sink_value(void *self,
                                const cmeta_type_desc *type,
                                const void *value) {
    cflow_sink_callbacks *cb = (cflow_sink_callbacks *)self;
    return cb && cb->on_value ? cb->on_value(cb->user, type, value) : true;
}
static void callback_sink_error(void *self, const char *message) {
    cflow_sink_callbacks *cb = (cflow_sink_callbacks *)self;
    if (cb && cb->on_error) cb->on_error(cb->user, message);
}
static void callback_sink_done(void *self) {
    cflow_sink_callbacks *cb = (cflow_sink_callbacks *)self;
    if (cb && cb->on_done) cb->on_done(cb->user);
}

CMETA_IMPLEMENTS(cflow_sink, callback_sink, 0,
    .value = callback_sink_value,
    .error = callback_sink_error,
    .done = callback_sink_done
);

cflow_sink cflow_sink_from_callbacks(cflow_sink_callbacks *callbacks) {
    return callback_sink_as_cflow_sink(callbacks);
}


typedef struct cflow_source_resumable_state {
    cflow_source source;
} cflow_source_resumable_state;

static cflow_step source_resumable_resume(void *self,
                                          cflow_resume_ctx *ctx,
                                          void *out_value) {
    cflow_source_resumable_state *s = (cflow_source_resumable_state *)self;
    return s ? cflow_source_resume(&s->source, ctx, out_value)
             : (cflow_step){ CFLOW_STEP_ERROR, {0}, "source-resumable state is null" };
}
static void source_resumable_cancel(void *self) {
    cflow_source_resumable_state *s = (cflow_source_resumable_state *)self;
    if (s && cflow_source_valid(&s->source)) cflow_source_cancel(&s->source);
}
static void source_resumable_destroy(void *self) {
    cflow_source_resumable_state *s = (cflow_source_resumable_state *)self;
    if (!s) return;
    if (cflow_source_valid(&s->source)) cflow_source_destroy(&s->source);
    free(s);
}
static const cflow_resumable_ops source_resumable_ops = {
    source_resumable_resume,
    source_resumable_cancel,
    source_resumable_destroy
};

bool cflow_resumable_from_source(cflow_resumable *out, cflow_source *source) {
    if (!out || !source || !cflow_source_valid(source)) return false;
    cflow_source_resumable_state *s = calloc(1, sizeof(*s));
    if (!s) return false;
    const cmeta_type_desc *type = cflow_source_output_type(source);
    const char *name = cflow_source_name(source);
    s->source = *source;
    memset(source, 0, sizeof(*source));
    *out = (cflow_resumable){ name, type, &source_resumable_ops, s };
    return true;
}
