#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
/* Darwin exposes the deprecated ucontext API through X/Open Issue 6. */
#define _XOPEN_SOURCE 600
#endif

#include <cflow/minicoro.h>

/* Minicoro's x64 assembly backend emits fixed global context-switch symbols.
 * Use its supported host fallbacks where those symbols can escape a static
 * archive and collide with another embedded minicoro implementation. */
#if defined(_WIN32)
#define MCO_USE_FIBERS
#elif defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#define MCO_USE_UCONTEXT
#endif

#if defined(__GNUC__) || defined(__clang__)
#define MCO_API static __attribute__((unused))
#else
#define MCO_API static
#endif
#define MINICORO_IMPL
#include "minicoro.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum cflow_minicoro_signal {
    CFLOW_MINICORO_SIGNAL_NONE = 0,
    CFLOW_MINICORO_SIGNAL_VALUE,
    CFLOW_MINICORO_SIGNAL_VALUE_AND_DONE,
    CFLOW_MINICORO_SIGNAL_WAIT,
    CFLOW_MINICORO_SIGNAL_ERROR
} cflow_minicoro_signal;

struct cflow_minicoro {
    mco_coro *frame;
    const char *name;
    const cmeta_type_desc *output_type;
    cflow_minicoro_entry_fn entry;
    void *user;
    cflow_resume_ctx *resume_context;
    const void *pending_value;
    cflow_waitable pending_waitable;
    const char *error;
    cflow_minicoro_alloc_fn alloc;
    cflow_minicoro_dealloc_fn dealloc;
    void *allocator_data;
    cflow_minicoro_signal signal;
    bool running;
    bool terminal;
    bool cancelled;
};

static void *minicoro_default_alloc(size_t size, void *allocator_data) {
    (void)allocator_data;
    return malloc(size);
}

static void minicoro_default_dealloc(void *pointer,
                                     size_t size,
                                     void *allocator_data) {
    (void)size;
    (void)allocator_data;
    free(pointer);
}

static void *minicoro_frame_alloc(size_t size, void *allocator_data) {
    cflow_minicoro *coroutine = (cflow_minicoro *)allocator_data;

    return coroutine != NULL
               ? coroutine->alloc(size, coroutine->allocator_data)
               : NULL;
}

static void minicoro_frame_dealloc(void *pointer,
                                   size_t size,
                                   void *allocator_data) {
    cflow_minicoro *coroutine = (cflow_minicoro *)allocator_data;

    if (coroutine != NULL)
        coroutine->dealloc(pointer, size, coroutine->allocator_data);
}

static cflow_step minicoro_step(cflow_step_kind kind,
                                cflow_waitable waitable,
                                const char *error) {
    cflow_step step = {kind, waitable, error};
    return step;
}

static cflow_step minicoro_error_step(cflow_minicoro *coroutine,
                                      const char *error) {
    if (coroutine != NULL) {
        coroutine->terminal = true;
        coroutine->error = error;
    }
    return minicoro_step(CFLOW_STEP_ERROR, (cflow_waitable){0}, error);
}

static bool minicoro_suspend(cflow_minicoro *coroutine,
                             cflow_minicoro_signal signal) {
    mco_result result;

    if (coroutine == NULL || coroutine->frame == NULL ||
        !coroutine->running || coroutine->terminal || coroutine->cancelled ||
        coroutine->signal != CFLOW_MINICORO_SIGNAL_NONE ||
        mco_running() != coroutine->frame)
        return false;

    coroutine->signal = signal;
    result = mco_yield(coroutine->frame);
    if (result != MCO_SUCCESS) {
        coroutine->signal = CFLOW_MINICORO_SIGNAL_ERROR;
        coroutine->error = "minicoro yield failed";
        return false;
    }
    return !coroutine->cancelled;
}

static void minicoro_entry(mco_coro *frame) {
    cflow_minicoro *coroutine =
        (cflow_minicoro *)mco_get_user_data(frame);

    if (coroutine == NULL || coroutine->entry == NULL)
        return;
    coroutine->entry(coroutine, coroutine->user);
}

static cflow_step minicoro_resume(void *state,
                                  cflow_resume_ctx *context,
                                  void *out_value) {
    cflow_minicoro *coroutine = (cflow_minicoro *)state;
    cflow_minicoro_signal signal;
    mco_result result;

    if (coroutine == NULL || coroutine->frame == NULL)
        return minicoro_step(CFLOW_STEP_ERROR, (cflow_waitable){0},
                             "minicoro state is invalid");
    if (out_value == NULL)
        return minicoro_error_step(coroutine,
                                   "minicoro output storage is null");
    if (coroutine->terminal || coroutine->cancelled)
        return minicoro_step(CFLOW_STEP_DONE, (cflow_waitable){0}, NULL);
    if (coroutine->running)
        return minicoro_error_step(coroutine,
                                   "minicoro resume is reentrant");

    coroutine->signal = CFLOW_MINICORO_SIGNAL_NONE;
    coroutine->pending_value = NULL;
    coroutine->pending_waitable = (cflow_waitable){0};
    coroutine->error = NULL;
    coroutine->resume_context = context;
    coroutine->running = true;
    result = mco_resume(coroutine->frame);
    coroutine->running = false;
    coroutine->resume_context = NULL;

    if (result != MCO_SUCCESS)
        return minicoro_error_step(coroutine,
                                   "minicoro resume failed");

    signal = coroutine->signal;
    coroutine->signal = CFLOW_MINICORO_SIGNAL_NONE;
    switch (signal) {
        case CFLOW_MINICORO_SIGNAL_VALUE:
            if (coroutine->pending_value == NULL)
                return minicoro_error_step(coroutine,
                                           "minicoro yielded no value");
            memcpy(out_value, coroutine->pending_value,
                   coroutine->output_type->size);
            coroutine->pending_value = NULL;
            return minicoro_step(CFLOW_STEP_VALUE,
                                 (cflow_waitable){0}, NULL);
        case CFLOW_MINICORO_SIGNAL_VALUE_AND_DONE:
            if (coroutine->pending_value == NULL)
                return minicoro_error_step(coroutine,
                                           "minicoro returned no value");
            memcpy(out_value, coroutine->pending_value,
                   coroutine->output_type->size);
            coroutine->pending_value = NULL;
            coroutine->terminal = true;
            return minicoro_step(CFLOW_STEP_VALUE_AND_DONE,
                                 (cflow_waitable){0}, NULL);
        case CFLOW_MINICORO_SIGNAL_WAIT:
            return minicoro_step(CFLOW_STEP_WAIT,
                                 coroutine->pending_waitable, NULL);
        case CFLOW_MINICORO_SIGNAL_ERROR:
            return minicoro_error_step(
                coroutine,
                coroutine->error != NULL ? coroutine->error
                                         : "minicoro callback failed");
        case CFLOW_MINICORO_SIGNAL_NONE:
            if (mco_status(coroutine->frame) == MCO_DEAD) {
                coroutine->terminal = true;
                return minicoro_step(CFLOW_STEP_DONE,
                                     (cflow_waitable){0}, NULL);
            }
            return minicoro_error_step(
                coroutine, "minicoro suspended without a CFlow step");
    }
    return minicoro_error_step(coroutine,
                               "minicoro returned an invalid signal");
}

static void minicoro_cancel(void *state) {
    cflow_minicoro *coroutine = (cflow_minicoro *)state;
    cflow_waitable waitable;

    if (coroutine == NULL)
        return;
    if (coroutine->cancelled)
        return;
    coroutine->cancelled = true;
    coroutine->terminal = true;
    waitable = coroutine->pending_waitable;
    coroutine->pending_waitable = (cflow_waitable){0};
    if (cflow_waitable_valid(&waitable))
        cflow_waitable_cancel(&waitable);
}

static void minicoro_destroy(void *state) {
    cflow_minicoro *coroutine = (cflow_minicoro *)state;
    cflow_minicoro_dealloc_fn dealloc;
    void *allocator_data;

    if (coroutine == NULL)
        return;
    minicoro_cancel(coroutine);
    if (coroutine->frame != NULL)
        (void)mco_destroy(coroutine->frame);
    dealloc = coroutine->dealloc;
    allocator_data = coroutine->allocator_data;
    dealloc(coroutine, sizeof(*coroutine), allocator_data);
}

static const cflow_resumable_ops minicoro_ops = {
    minicoro_resume,
    minicoro_cancel,
    minicoro_destroy
};

bool cflow_resumable_from_minicoro(cflow_resumable *out,
                                    const cflow_minicoro_config *config) {
    const cmeta_trait_flags required = CMETA_TRAIT_TRIVIAL_COPY |
                                       CMETA_TRAIT_TRIVIAL_DESTROY;
    cflow_minicoro *coroutine;
    mco_desc descriptor;

    if (out == NULL || config == NULL || config->entry == NULL ||
        !cmeta_type_desc_valid(config->output_type) ||
        config->output_type->size == 0u ||
        (config->output_type->align & (config->output_type->align - 1u)) != 0u ||
        cmeta_type_require_traits(config->output_type, required) != CMETA_OK ||
        ((config->alloc == NULL) != (config->dealloc == NULL)) ||
        config->stack_size > SIZE_MAX - 15u)
        return false;

    descriptor = mco_desc_init(minicoro_entry, 0u);
    if (config->stack_size != 0u &&
        descriptor.coro_size > SIZE_MAX - 15u - config->stack_size)
        return false;

    {
        cflow_minicoro_alloc_fn alloc =
            config->alloc != NULL ? config->alloc : minicoro_default_alloc;
        coroutine = (cflow_minicoro *)alloc(sizeof(*coroutine),
                                             config->allocator_data);
    }
    if (coroutine == NULL)
        return false;
    memset(coroutine, 0, sizeof(*coroutine));
    coroutine->name = config->name != NULL ? config->name : "minicoro";
    coroutine->output_type = config->output_type;
    coroutine->entry = config->entry;
    coroutine->user = config->user;
    coroutine->alloc = config->alloc != NULL
                           ? config->alloc
                           : minicoro_default_alloc;
    coroutine->dealloc = config->dealloc != NULL
                             ? config->dealloc
                             : minicoro_default_dealloc;
    coroutine->allocator_data = config->allocator_data;

    descriptor = mco_desc_init(minicoro_entry, config->stack_size);
    descriptor.user_data = coroutine;
    descriptor.alloc_cb = minicoro_frame_alloc;
    descriptor.dealloc_cb = minicoro_frame_dealloc;
    descriptor.allocator_data = coroutine;
    if (mco_create(&coroutine->frame, &descriptor) != MCO_SUCCESS) {
        coroutine->dealloc(coroutine, sizeof(*coroutine),
                           coroutine->allocator_data);
        return false;
    }

    *out = (cflow_resumable){
        coroutine->name,
        coroutine->output_type,
        &minicoro_ops,
        coroutine
    };
    return true;
}

bool cflow_minicoro_yield_value(cflow_minicoro *coroutine,
                                const void *value) {
    if (coroutine == NULL || value == NULL)
        return false;
    coroutine->pending_value = value;
    if (!minicoro_suspend(coroutine, CFLOW_MINICORO_SIGNAL_VALUE)) {
        coroutine->pending_value = NULL;
        return false;
    }
    return true;
}

bool cflow_minicoro_return_value(cflow_minicoro *coroutine,
                                 const void *value) {
    if (coroutine == NULL || value == NULL)
        return false;
    coroutine->pending_value = value;
    if (!minicoro_suspend(coroutine,
                          CFLOW_MINICORO_SIGNAL_VALUE_AND_DONE)) {
        coroutine->pending_value = NULL;
        return false;
    }
    return true;
}

bool cflow_minicoro_wait(cflow_minicoro *coroutine,
                         cflow_waitable waitable) {
    if (coroutine == NULL || !cflow_waitable_valid(&waitable))
        return false;
    coroutine->pending_waitable = waitable;
    if (!minicoro_suspend(coroutine, CFLOW_MINICORO_SIGNAL_WAIT)) {
        coroutine->pending_waitable = (cflow_waitable){0};
        return false;
    }
    return true;
}

bool cflow_minicoro_fail(cflow_minicoro *coroutine,
                         const char *error) {
    if (coroutine == NULL || error == NULL)
        return false;
    coroutine->error = error;
    return minicoro_suspend(coroutine, CFLOW_MINICORO_SIGNAL_ERROR);
}

cflow_resume_ctx *cflow_minicoro_resume_context(
    cflow_minicoro *coroutine) {
    return coroutine != NULL && coroutine->running
               ? coroutine->resume_context
               : NULL;
}
