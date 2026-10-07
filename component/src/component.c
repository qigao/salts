#include <salts/component.h>

#include <string.h>

static salts_component_failure salts_component_failure_none(void) {
    salts_component_failure failure;
    failure.phase = SALTS_COMPONENT_PHASE_NONE;
    failure.component_index = SALTS_COMPONENT_INDEX_NONE;
    failure.dependency_index = SALTS_COMPONENT_INDEX_NONE;
    failure.provider_status = CMETA_OK;
    return failure;
}

static salts_component_status salts_component_fail(
    salts_component_context *context,
    salts_component_status status,
    salts_component_phase phase,
    size_t component_index,
    size_t dependency_index,
    cmeta_status provider_status) {
    if (context != NULL) {
        context->failure.phase = phase;
        context->failure.component_index = component_index;
        context->failure.dependency_index = dependency_index;
        context->failure.provider_status = provider_status;
        context->state = SALTS_COMPONENT_CONTEXT_FAILED;
    }
    return status;
}

static bool salts_component_role_present(
    const cmeta_component_desc *component,
    cmeta_component_role role) {
    size_t i;
    if (!cmeta_component_desc_valid(component))
        return false;
    for (i = 0u; i < component->capability_count; ++i)
        if (component->capabilities[i].role == role)
            return true;
    return false;
}

static bool salts_component_provides(
    const cmeta_component_desc *component,
    const cmeta_interface_desc *expected) {
    size_t i;
    if (!cmeta_component_desc_valid(component) ||
        !cmeta_interface_desc_valid(expected))
        return false;
    for (i = 0u; i < component->capability_count; ++i) {
        const cmeta_component_capability *row = &component->capabilities[i];
        if (row->role == CMETA_COMPONENT_PROVIDES &&
            cmeta_interface_desc_equal(row->interface_desc, expected))
            return true;
    }
    return false;
}

static size_t salts_component_requirement_count(
    const cmeta_component_desc *component) {
    size_t count = 0u;
    size_t i;
    for (i = 0u; i < component->capability_count; ++i)
        if (component->capabilities[i].role == CMETA_COMPONENT_REQUIRES)
            ++count;
    return count;
}

static salts_component_status salts_component_binding_status(
    const salts_component_provider_binding *binding) {
    const cmeta_component_desc *component;

    if (binding == NULL || !cmeta_component_desc_valid(binding->component) ||
        binding->create == NULL)
        return SALTS_COMPONENT_INVALID_COMPONENT;

    component = binding->component;

    if ((binding->activate == NULL) != (binding->deactivate == NULL))
        return SALTS_COMPONENT_INVALID_COMPONENT;

    if (component->config == NULL) {
        if (binding->config_data != NULL || binding->config_value != NULL)
            return SALTS_COMPONENT_CONFIG_MISMATCH;
    } else {
        if (binding->config_data == NULL || binding->config_value == NULL ||
            !cmeta_data_desc_valid(binding->config_data) ||
            !cmeta_data_desc_equal(component->config, binding->config_data))
            return SALTS_COMPONENT_CONFIG_MISMATCH;
    }

    if (binding->interfaces != NULL &&
        !cmeta_object_interface_provider_valid(binding->interfaces))
        return SALTS_COMPONENT_INVALID_COMPONENT;

    if (salts_component_role_present(component, CMETA_COMPONENT_PROVIDES) &&
        !cmeta_object_interface_provider_valid(binding->interfaces))
        return SALTS_COMPONENT_INVALID_COMPONENT;

    return SALTS_COMPONENT_OK;
}

static void salts_component_instance_reset(salts_component_instance *instance) {
    if (instance == NULL)
        return;
    instance->object = (cmeta_object_ref)CMETA_OBJECT_REF_INIT;
    instance->dependency_offset = 0u;
    instance->dependency_count = 0u;
    instance->active = false;
}

static bool salts_component_order_contains(
    const salts_component_context *context,
    size_t prefix_count,
    size_t component_index) {
    size_t i;
    for (i = 0u; i < prefix_count; ++i)
        if (context->activation_order[i] == component_index)
            return true;
    return false;
}

static bool salts_component_requires(
    const cmeta_component_desc *component,
    const cmeta_interface_desc *expected) {
    size_t i;
    if (!cmeta_component_desc_valid(component) ||
        !cmeta_interface_desc_valid(expected))
        return false;
    for (i = 0u; i < component->capability_count; ++i) {
        const cmeta_component_capability *row = &component->capabilities[i];
        if (row->role == CMETA_COMPONENT_REQUIRES &&
            cmeta_interface_desc_equal(row->interface_desc, expected))
            return true;
    }
    return false;
}

static size_t salts_component_find_id(
    const salts_component_context *context,
    const char *stable_id) {
    size_t i;
    if (context == NULL || stable_id == NULL)
        return SALTS_COMPONENT_INDEX_NONE;
    for (i = 0u; i < context->provider_count; ++i)
        if (strcmp(context->providers[i].component->stable_id, stable_id) == 0)
            return i;
    return SALTS_COMPONENT_INDEX_NONE;
}

static salts_component_status salts_component_select_provider(
    const salts_component_context *context,
    size_t consumer_index,
    const cmeta_interface_desc *requirement,
    size_t *out_provider) {
    const char *consumer_id;
    const salts_component_selection *selection = NULL;
    size_t selection_matches = 0u;
    size_t provider_candidate = SALTS_COMPONENT_INDEX_NONE;
    size_t provider_matches = 0u;
    size_t i;

    if (context == NULL || consumer_index >= context->provider_count ||
        !cmeta_interface_desc_valid(requirement) || out_provider == NULL)
        return SALTS_COMPONENT_INVALID_ARGUMENT;

    consumer_id = context->providers[consumer_index].component->stable_id;

    for (i = 0u; i < context->selection_count; ++i) {
        const salts_component_selection *row = &context->selections[i];
        if (strcmp(row->consumer_component_id, consumer_id) == 0 &&
            cmeta_interface_desc_equal(row->requirement, requirement)) {
            selection = row;
            ++selection_matches;
        }
    }

    if (selection_matches > 1u)
        return SALTS_COMPONENT_INVALID_SELECTION;

    if (selection_matches == 1u) {
        provider_candidate =
            salts_component_find_id(context, selection->provider_component_id);
        if (provider_candidate == SALTS_COMPONENT_INDEX_NONE ||
            !salts_component_provides(
                context->providers[provider_candidate].component, requirement))
            return SALTS_COMPONENT_INVALID_SELECTION;
        *out_provider = provider_candidate;
        return SALTS_COMPONENT_OK;
    }

    for (i = 0u; i < context->provider_count; ++i) {
        if (salts_component_provides(
                context->providers[i].component, requirement)) {
            provider_candidate = i;
            ++provider_matches;
        }
    }

    if (provider_matches == 0u)
        return SALTS_COMPONENT_MISSING_PROVIDER;
    if (provider_matches != 1u)
        return SALTS_COMPONENT_AMBIGUOUS_PROVIDER;

    *out_provider = provider_candidate;
    return SALTS_COMPONENT_OK;
}

static bool salts_component_dependencies_ready(
    const salts_component_context *context,
    size_t component_index,
    size_t prefix_count) {
    const salts_component_instance *instance = &context->instances[component_index];
    size_t i;

    for (i = 0u; i < instance->dependency_count; ++i) {
        const salts_component_dependency *dependency =
            &context->dependencies[instance->dependency_offset + i];
        if (!salts_component_order_contains(
                context, prefix_count, dependency->provider_index))
            return false;
    }
    return true;
}

static cmeta_status salts_component_preflight_provides(
    const salts_component_provider_binding *binding,
    const cmeta_object_ref *instance) {
    size_t i;

    for (i = 0u; i < binding->component->capability_count; ++i) {
        const cmeta_component_capability *capability =
            &binding->component->capabilities[i];
        cmeta_interface_projection projection =
            CMETA_INTERFACE_PROJECTION_INIT;

        if (capability->role != CMETA_COMPONENT_PROVIDES)
            continue;

        {
            cmeta_status status = cmeta_object_interface_project_borrowed(
                instance, binding->interfaces,
                capability->interface_desc, &projection);
            if (status != CMETA_OK)
                return status;
        }
    }

    return CMETA_OK;
}

static void salts_component_rollback(
    salts_component_context *context,
    size_t activated_count) {
    while (activated_count != 0u) {
        size_t component_index;
        salts_component_instance *instance;
        const salts_component_provider_binding *binding;

        --activated_count;
        component_index = context->activation_order[activated_count];
        instance = &context->instances[component_index];
        binding = &context->providers[component_index];

        if (!instance->active)
            continue;

        if (binding->deactivate != NULL)
            binding->deactivate(binding->provider_context, &instance->object);
        instance->active = false;
        cmeta_object_release(&instance->object);
    }
    context->activation_count = 0u;
}

const char *salts_component_status_string(salts_component_status status) {
    switch (status) {
    case SALTS_COMPONENT_OK: return "ok";
    case SALTS_COMPONENT_INVALID_ARGUMENT: return "invalid argument";
    case SALTS_COMPONENT_INVALID_COMPONENT: return "invalid component";
    case SALTS_COMPONENT_DUPLICATE_COMPONENT_ID: return "duplicate component id";
    case SALTS_COMPONENT_INVALID_SELECTION: return "invalid provider selection";
    case SALTS_COMPONENT_CONFIG_MISMATCH: return "configuration mismatch";
    case SALTS_COMPONENT_CAPACITY_EXCEEDED: return "capacity exceeded";
    case SALTS_COMPONENT_MISSING_PROVIDER: return "missing provider";
    case SALTS_COMPONENT_AMBIGUOUS_PROVIDER: return "ambiguous provider";
    case SALTS_COMPONENT_DEPENDENCY_CYCLE: return "dependency cycle";
    case SALTS_COMPONENT_INTERFACE_UNAVAILABLE: return "interface unavailable";
    case SALTS_COMPONENT_CREATE_FAILED: return "create failed";
    case SALTS_COMPONENT_ACTIVATE_FAILED: return "activate failed";
    case SALTS_COMPONENT_INVALID_STATE: return "invalid state";
    }
    return "unknown component status";
}

salts_component_status salts_component_context_init(
    salts_component_context *context,
    const salts_component_provider_binding *providers,
    size_t provider_count,
    const salts_component_selection *selections,
    size_t selection_count,
    salts_component_instance *instances,
    size_t instance_capacity,
    salts_component_dependency *dependencies,
    size_t dependency_capacity,
    size_t *activation_order,
    size_t activation_capacity) {
    size_t i;
    size_t j;

    if (context == NULL)
        return SALTS_COMPONENT_INVALID_ARGUMENT;

    memset(context, 0, sizeof(*context));
    context->failure = salts_component_failure_none();

    if ((provider_count != 0u &&
         (providers == NULL || instances == NULL || activation_order == NULL)) ||
        (selection_count != 0u && selections == NULL) ||
        (dependency_capacity != 0u && dependencies == NULL))
        return salts_component_fail(
            context, SALTS_COMPONENT_INVALID_ARGUMENT,
            SALTS_COMPONENT_PHASE_INIT, SALTS_COMPONENT_INDEX_NONE,
            SALTS_COMPONENT_INDEX_NONE, CMETA_OK);

    if (provider_count > instance_capacity ||
        provider_count > activation_capacity)
        return salts_component_fail(
            context, SALTS_COMPONENT_CAPACITY_EXCEEDED,
            SALTS_COMPONENT_PHASE_INIT, SALTS_COMPONENT_INDEX_NONE,
            SALTS_COMPONENT_INDEX_NONE, CMETA_OK);

    context->providers = providers;
    context->provider_count = provider_count;
    context->selections = selections;
    context->selection_count = selection_count;
    context->instances = instances;
    context->instance_capacity = instance_capacity;
    context->dependencies = dependencies;
    context->dependency_capacity = dependency_capacity;
    context->activation_order = activation_order;
    context->activation_capacity = activation_capacity;

    for (i = 0u; i < provider_count; ++i) {
        salts_component_status status =
            salts_component_binding_status(&providers[i]);
        if (status != SALTS_COMPONENT_OK)
            return salts_component_fail(
                context, status, SALTS_COMPONENT_PHASE_INIT, i,
                SALTS_COMPONENT_INDEX_NONE, CMETA_OK);

        for (j = 0u; j < i; ++j) {
            if (strcmp(
                    providers[i].component->stable_id,
                    providers[j].component->stable_id) == 0)
                return salts_component_fail(
                    context, SALTS_COMPONENT_DUPLICATE_COMPONENT_ID,
                    SALTS_COMPONENT_PHASE_INIT, i,
                    SALTS_COMPONENT_INDEX_NONE, CMETA_OK);
        }

        salts_component_instance_reset(&instances[i]);
    }

    for (i = 0u; i < selection_count; ++i) {
        const salts_component_selection *selection = &selections[i];
        size_t consumer_index;
        size_t provider_index;

        if (selection->consumer_component_id == NULL ||
            selection->consumer_component_id[0] == '\0' ||
            selection->provider_component_id == NULL ||
            selection->provider_component_id[0] == '\0' ||
            !cmeta_interface_desc_valid(selection->requirement))
            return salts_component_fail(
                context, SALTS_COMPONENT_INVALID_SELECTION,
                SALTS_COMPONENT_PHASE_INIT, SALTS_COMPONENT_INDEX_NONE,
                i, CMETA_OK);

        consumer_index =
            salts_component_find_id(context, selection->consumer_component_id);
        provider_index =
            salts_component_find_id(context, selection->provider_component_id);

        if (consumer_index == SALTS_COMPONENT_INDEX_NONE ||
            provider_index == SALTS_COMPONENT_INDEX_NONE ||
            !salts_component_requires(
                providers[consumer_index].component, selection->requirement) ||
            !salts_component_provides(
                providers[provider_index].component, selection->requirement))
            return salts_component_fail(
                context, SALTS_COMPONENT_INVALID_SELECTION,
                SALTS_COMPONENT_PHASE_INIT,
                consumer_index, i, CMETA_OK);

        for (j = 0u; j < i; ++j) {
            if (strcmp(
                    selections[j].consumer_component_id,
                    selection->consumer_component_id) == 0 &&
                cmeta_interface_desc_equal(
                    selections[j].requirement, selection->requirement))
                return salts_component_fail(
                    context, SALTS_COMPONENT_INVALID_SELECTION,
                    SALTS_COMPONENT_PHASE_INIT,
                    consumer_index, i, CMETA_OK);
        }
    }

    context->state = SALTS_COMPONENT_CONTEXT_READY;
    return SALTS_COMPONENT_OK;
}

salts_component_status salts_component_context_resolve(
    salts_component_context *context) {
    size_t required_total = 0u;
    size_t dependency_count = 0u;
    size_t i;

    if (context == NULL)
        return SALTS_COMPONENT_INVALID_ARGUMENT;
    if (context->state != SALTS_COMPONENT_CONTEXT_READY)
        return SALTS_COMPONENT_INVALID_STATE;

    context->failure = salts_component_failure_none();
    context->dependency_count = 0u;
    context->activation_count = 0u;

    for (i = 0u; i < context->provider_count; ++i) {
        size_t count =
            salts_component_requirement_count(context->providers[i].component);
        if (required_total > SIZE_MAX - count)
            return salts_component_fail(
                context, SALTS_COMPONENT_CAPACITY_EXCEEDED,
                SALTS_COMPONENT_PHASE_RESOLVE, i,
                SALTS_COMPONENT_INDEX_NONE, CMETA_OK);
        required_total += count;
    }

    if (required_total > context->dependency_capacity ||
        (required_total != 0u && context->dependencies == NULL))
        return salts_component_fail(
            context, SALTS_COMPONENT_CAPACITY_EXCEEDED,
            SALTS_COMPONENT_PHASE_RESOLVE, SALTS_COMPONENT_INDEX_NONE,
            SALTS_COMPONENT_INDEX_NONE, CMETA_OK);

    for (i = 0u; i < context->provider_count; ++i) {
        const cmeta_component_desc *consumer = context->providers[i].component;
        salts_component_instance *instance = &context->instances[i];
        size_t capability_index;

        instance->dependency_offset = dependency_count;
        instance->dependency_count = 0u;

        for (capability_index = 0u;
             capability_index < consumer->capability_count;
             ++capability_index) {
            const cmeta_component_capability *requirement =
                &consumer->capabilities[capability_index];
            size_t candidate = SALTS_COMPONENT_INDEX_NONE;
            salts_component_status selection_status;

            if (requirement->role != CMETA_COMPONENT_REQUIRES)
                continue;

            selection_status = salts_component_select_provider(
                context, i, requirement->interface_desc, &candidate);
            if (selection_status != SALTS_COMPONENT_OK)
                return salts_component_fail(
                    context, selection_status,
                    SALTS_COMPONENT_PHASE_RESOLVE, i, dependency_count,
                    CMETA_OK);

            context->dependencies[dependency_count].consumer_index = i;
            context->dependencies[dependency_count].provider_index = candidate;
            context->dependencies[dependency_count].interface_desc =
                requirement->interface_desc;
            context->dependencies[dependency_count].provider_component =
                context->providers[candidate].component;
            context->dependencies[dependency_count].provider_instance =
                &context->instances[candidate].object;
            context->dependencies[dependency_count].provider_interfaces =
                context->providers[candidate].interfaces;
            ++dependency_count;
            ++instance->dependency_count;
        }
    }

    context->dependency_count = dependency_count;

    for (i = 0u; i < context->provider_count; ++i) {
        size_t candidate;
        size_t selected = SALTS_COMPONENT_INDEX_NONE;

        for (candidate = 0u;
             candidate < context->provider_count;
             ++candidate) {
            if (salts_component_order_contains(context, i, candidate))
                continue;
            if (salts_component_dependencies_ready(context, candidate, i)) {
                selected = candidate;
                break;
            }
        }

        if (selected == SALTS_COMPONENT_INDEX_NONE)
            return salts_component_fail(
                context, SALTS_COMPONENT_DEPENDENCY_CYCLE,
                SALTS_COMPONENT_PHASE_RESOLVE, SALTS_COMPONENT_INDEX_NONE,
                SALTS_COMPONENT_INDEX_NONE, CMETA_OK);

        context->activation_order[i] = selected;
    }

    context->activation_count = context->provider_count;
    context->state = SALTS_COMPONENT_CONTEXT_RESOLVED;
    return SALTS_COMPONENT_OK;
}

salts_component_status salts_component_context_start(
    salts_component_context *context) {
    size_t order_index;

    if (context == NULL)
        return SALTS_COMPONENT_INVALID_ARGUMENT;
    if (context->state != SALTS_COMPONENT_CONTEXT_RESOLVED)
        return SALTS_COMPONENT_INVALID_STATE;

    context->failure = salts_component_failure_none();

    for (order_index = 0u;
         order_index < context->activation_count;
         ++order_index) {
        const size_t component_index = context->activation_order[order_index];
        const salts_component_provider_binding *binding =
            &context->providers[component_index];
        salts_component_instance *instance =
            &context->instances[component_index];
        const salts_component_dependency *dependencies =
            instance->dependency_count == 0u
                ? NULL
                : &context->dependencies[instance->dependency_offset];
        size_t dependency_index;
        cmeta_status provider_status;

        for (dependency_index = 0u;
             dependency_index < instance->dependency_count;
             ++dependency_index) {
            cmeta_interface_projection projection =
                CMETA_INTERFACE_PROJECTION_INIT;
            const salts_component_dependency *dependency =
                &dependencies[dependency_index];

            provider_status = cmeta_object_interface_project_borrowed(
                dependency->provider_instance,
                dependency->provider_interfaces,
                dependency->interface_desc,
                &projection);
            if (provider_status != CMETA_OK) {
                salts_component_rollback(context, order_index);
                return salts_component_fail(
                    context, SALTS_COMPONENT_INTERFACE_UNAVAILABLE,
                    SALTS_COMPONENT_PHASE_DEPENDENCY, component_index,
                    instance->dependency_offset + dependency_index,
                    provider_status);
            }
        }

        instance->object = (cmeta_object_ref)CMETA_OBJECT_REF_INIT;
        provider_status = binding->create(
            binding->provider_context,
            binding->config_data,
            binding->config_value,
            dependencies,
            instance->dependency_count,
            &instance->object);

        if (provider_status != CMETA_OK) {
            if (cmeta_object_ref_valid(&instance->object))
                cmeta_object_release(&instance->object);
            salts_component_rollback(context, order_index);
            return salts_component_fail(
                context, SALTS_COMPONENT_CREATE_FAILED,
                SALTS_COMPONENT_PHASE_CREATE, component_index,
                SALTS_COMPONENT_INDEX_NONE, provider_status);
        }

        if (!cmeta_object_ref_valid(&instance->object)) {
            salts_component_rollback(context, order_index);
            return salts_component_fail(
                context, SALTS_COMPONENT_CREATE_FAILED,
                SALTS_COMPONENT_PHASE_CREATE, component_index,
                SALTS_COMPONENT_INDEX_NONE, CMETA_CALLBACK_ERROR);
        }

        if (binding->activate != NULL) {
            provider_status =
                binding->activate(binding->provider_context, &instance->object);
            if (provider_status != CMETA_OK) {
                cmeta_object_release(&instance->object);
                salts_component_rollback(context, order_index);
                return salts_component_fail(
                    context, SALTS_COMPONENT_ACTIVATE_FAILED,
                    SALTS_COMPONENT_PHASE_ACTIVATE, component_index,
                    SALTS_COMPONENT_INDEX_NONE, provider_status);
            }
        }

        provider_status =
            salts_component_preflight_provides(binding, &instance->object);
        if (provider_status != CMETA_OK) {
            if (binding->deactivate != NULL)
                binding->deactivate(
                    binding->provider_context, &instance->object);
            cmeta_object_release(&instance->object);
            salts_component_rollback(context, order_index);
            return salts_component_fail(
                context, SALTS_COMPONENT_INTERFACE_UNAVAILABLE,
                SALTS_COMPONENT_PHASE_PROVIDE, component_index,
                SALTS_COMPONENT_INDEX_NONE, provider_status);
        }

        instance->active = true;
    }

    context->state = SALTS_COMPONENT_CONTEXT_ACTIVE;
    return SALTS_COMPONENT_OK;
}

salts_component_status salts_component_context_stop(
    salts_component_context *context) {
    size_t order_index;

    if (context == NULL)
        return SALTS_COMPONENT_INVALID_ARGUMENT;
    if (context->state != SALTS_COMPONENT_CONTEXT_ACTIVE)
        return SALTS_COMPONENT_INVALID_STATE;

    order_index = context->activation_count;
    while (order_index != 0u) {
        size_t component_index;
        salts_component_instance *instance;
        const salts_component_provider_binding *binding;

        --order_index;
        component_index = context->activation_order[order_index];
        instance = &context->instances[component_index];
        binding = &context->providers[component_index];

        if (!instance->active)
            continue;

        if (binding->deactivate != NULL)
            binding->deactivate(binding->provider_context, &instance->object);
        instance->active = false;
        cmeta_object_release(&instance->object);
    }

    context->state = SALTS_COMPONENT_CONTEXT_STOPPED;
    return SALTS_COMPONENT_OK;
}

salts_component_status salts_component_context_find_service(
    const salts_component_context *context,
    const cmeta_interface_desc *interface_desc,
    salts_component_service *out_service) {
    salts_component_service candidate_service;
    size_t candidate = SALTS_COMPONENT_INDEX_NONE;
    size_t candidate_count = 0u;
    size_t i;

    if (context == NULL || out_service == NULL ||
        !cmeta_interface_desc_valid(interface_desc))
        return SALTS_COMPONENT_INVALID_ARGUMENT;
    if (context->state != SALTS_COMPONENT_CONTEXT_ACTIVE)
        return SALTS_COMPONENT_INVALID_STATE;

    for (i = 0u; i < context->provider_count; ++i) {
        if (salts_component_provides(
                context->providers[i].component, interface_desc)) {
            candidate = i;
            ++candidate_count;
        }
    }

    if (candidate_count == 0u)
        return SALTS_COMPONENT_MISSING_PROVIDER;
    if (candidate_count != 1u)
        return SALTS_COMPONENT_AMBIGUOUS_PROVIDER;

    if (!context->instances[candidate].active)
        return SALTS_COMPONENT_INVALID_STATE;

    {
        cmeta_interface_projection projection =
            CMETA_INTERFACE_PROJECTION_INIT;
        cmeta_status status = cmeta_object_interface_project_borrowed(
            &context->instances[candidate].object,
            context->providers[candidate].interfaces,
            interface_desc,
            &projection);
        if (status != CMETA_OK)
            return SALTS_COMPONENT_INTERFACE_UNAVAILABLE;
    }

    candidate_service.component = context->providers[candidate].component;
    candidate_service.object = &context->instances[candidate].object;
    candidate_service.interfaces = context->providers[candidate].interfaces;
    *out_service = candidate_service;
    return SALTS_COMPONENT_OK;
}

salts_component_status salts_component_dependency_find(
    const salts_component_dependency *dependencies,
    size_t dependency_count,
    const cmeta_interface_desc *interface_desc,
    const salts_component_dependency **out_dependency) {
    const salts_component_dependency *candidate = NULL;
    size_t candidate_count = 0u;
    size_t i;

    if (out_dependency == NULL || !cmeta_interface_desc_valid(interface_desc) ||
        (dependency_count != 0u && dependencies == NULL))
        return SALTS_COMPONENT_INVALID_ARGUMENT;

    for (i = 0u; i < dependency_count; ++i) {
        if (cmeta_interface_desc_equal(
                dependencies[i].interface_desc, interface_desc)) {
            candidate = &dependencies[i];
            ++candidate_count;
        }
    }

    if (candidate_count == 0u)
        return SALTS_COMPONENT_MISSING_PROVIDER;
    if (candidate_count != 1u)
        return SALTS_COMPONENT_AMBIGUOUS_PROVIDER;

    *out_dependency = candidate;
    return SALTS_COMPONENT_OK;
}

const salts_component_failure *salts_component_context_failure(
    const salts_component_context *context) {
    return context == NULL ? NULL : &context->failure;
}
