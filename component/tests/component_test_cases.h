#ifndef SALTS_COMPONENT_TEST_CASES_H
#define SALTS_COMPONENT_TEST_CASES_H

#include "component_test_fixture.h"

suite("Salts static Component Configurator") {
    it("resolves, activates and tears down dependencies deterministically") {
        unsigned clock = 0u;
        int app_config = 5;
        test_provider_state logger;
        test_provider_state app;
        salts_component_provider_binding providers[2];
        salts_component_instance instances[2];
        salts_component_dependency dependencies[2];
        size_t order[2];
        salts_component_context context;
        salts_component_service service;
        test_app app_interface = test_app_bind(NULL, NULL);

        test_provider_state_init(&logger, 7, &clock);
        test_provider_state_init(&app, 0, &clock);

        providers[0] = (salts_component_provider_binding){
            cmeta_component_meta(TestLogger), &logger,
            NULL, NULL, &test_interfaces,
            test_logger_create, test_activate, test_deactivate
        };
        providers[1] = (salts_component_provider_binding){
            cmeta_component_meta(TestApp), &app,
            &cmeta_data_int, &app_config, &test_interfaces,
            test_app_create, test_activate, test_deactivate
        };

        check_equal(salts_component_context_init(
            &context, providers, 2u, instances, 2u,
            dependencies, 2u, order, 2u), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_resolve(&context), SALTS_COMPONENT_OK);
        check_equal(order[0], (size_t)0u);
        check_equal(order[1], (size_t)1u);

        check_equal(salts_component_context_start(&context), SALTS_COMPONENT_OK);
        check_equal(context.state, SALTS_COMPONENT_CONTEXT_ACTIVE);
        check_equal(logger.sequence, 1u);
        check_equal(app.sequence, 2u);
        check_equal(app.value, 12);

        check_equal(salts_component_context_find_service(
            &context, test_app_interface(), &service), SALTS_COMPONENT_OK);
        check_equal(test_app_borrow_from_object(
            service.object, service.interfaces, &app_interface), CMETA_OK);
        check_equal(test_app_value(&app_interface), 12);

        check_equal(salts_component_context_stop(&context), SALTS_COMPONENT_OK);
        check_equal(logger.deactivates, 1u);
        check_equal(app.deactivates, 1u);
        check_equal(logger.destroys, 1u);
        check_equal(app.destroys, 1u);
        check_equal(context.state, SALTS_COMPONENT_CONTEXT_STOPPED);
    }

    it("fails closed when a required provider is missing") {
        int config = 1;
        test_provider_state app;
        salts_component_provider_binding provider;
        salts_component_instance instance;
        salts_component_dependency dependency;
        size_t order;
        salts_component_context context;

        test_provider_state_init(&app, 0, NULL);
        provider = (salts_component_provider_binding){
            cmeta_component_meta(TestApp), &app,
            &cmeta_data_int, &config, &test_interfaces,
            test_app_create, test_activate, test_deactivate
        };

        check_equal(salts_component_context_init(
            &context, &provider, 1u, &instance, 1u,
            &dependency, 1u, &order, 1u), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_resolve(&context),
                    SALTS_COMPONENT_MISSING_PROVIDER);
        check_equal(context.state, SALTS_COMPONENT_CONTEXT_FAILED);
    }

    it("rejects ambiguous providers instead of ranking implicitly") {
        int config = 1;
        test_provider_state logger_a;
        test_provider_state logger_b;
        test_provider_state app;
        salts_component_provider_binding providers[3];
        salts_component_instance instances[3];
        salts_component_dependency dependencies[2];
        size_t order[3];
        salts_component_context context;

        test_provider_state_init(&logger_a, 1, NULL);
        test_provider_state_init(&logger_b, 2, NULL);
        test_provider_state_init(&app, 0, NULL);

        providers[0] = (salts_component_provider_binding){
            cmeta_component_meta(TestLogger), &logger_a,
            NULL, NULL, &test_interfaces,
            test_logger_create, test_activate, test_deactivate
        };
        providers[1] = (salts_component_provider_binding){
            cmeta_component_meta(TestLoggerAlt), &logger_b,
            NULL, NULL, &test_interfaces,
            test_logger_create, test_activate, test_deactivate
        };
        providers[2] = (salts_component_provider_binding){
            cmeta_component_meta(TestApp), &app,
            &cmeta_data_int, &config, &test_interfaces,
            test_app_create, test_activate, test_deactivate
        };

        check_equal(salts_component_context_init(
            &context, providers, 3u, instances, 3u,
            dependencies, 2u, order, 3u), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_resolve(&context),
                    SALTS_COMPONENT_AMBIGUOUS_PROVIDER);
    }

    it("detects dependency cycles before any create callback") {
        test_provider_state a;
        test_provider_state b;
        salts_component_provider_binding providers[2];
        salts_component_instance instances[2];
        salts_component_dependency dependencies[2];
        size_t order[2];
        salts_component_context context;

        test_provider_state_init(&a, 1, NULL);
        test_provider_state_init(&b, 2, NULL);

        providers[0] = (salts_component_provider_binding){
            cmeta_component_meta(TestCycleA), &a,
            NULL, NULL, &test_interfaces,
            test_cycle_create, test_activate, test_deactivate
        };
        providers[1] = (salts_component_provider_binding){
            cmeta_component_meta(TestCycleB), &b,
            NULL, NULL, &test_interfaces,
            test_cycle_create, test_activate, test_deactivate
        };

        check_equal(salts_component_context_init(
            &context, providers, 2u, instances, 2u,
            dependencies, 2u, order, 2u), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_resolve(&context),
                    SALTS_COMPONENT_DEPENDENCY_CYCLE);
        check_equal(a.creates, 0u);
        check_equal(b.creates, 0u);
    }

    it("rejects a configuration type mismatch before resolution") {
        bool wrong_config = true;
        test_provider_state app;
        salts_component_provider_binding provider;
        salts_component_instance instance;
        salts_component_dependency dependency;
        size_t order;
        salts_component_context context;

        test_provider_state_init(&app, 0, NULL);
        provider = (salts_component_provider_binding){
            cmeta_component_meta(TestApp), &app,
            &cmeta_data_bool, &wrong_config, &test_interfaces,
            test_app_create, test_activate, test_deactivate
        };

        check_equal(salts_component_context_init(
            &context, &provider, 1u, &instance, 1u,
            &dependency, 1u, &order, 1u), SALTS_COMPONENT_CONFIG_MISMATCH);
        check_equal(app.creates, 0u);
    }

    it("rejects a provider that cannot project its declared service") {
        test_provider_state broken;
        salts_component_provider_binding provider;
        salts_component_instance instance;
        size_t order;
        salts_component_context context;

        test_provider_state_init(&broken, 9, NULL);
        provider = (salts_component_provider_binding){
            cmeta_component_meta(TestBrokenProvider), &broken,
            NULL, NULL, &test_interfaces,
            test_cycle_create, test_activate, test_deactivate
        };

        check_equal(salts_component_context_init(
            &context, &provider, 1u, &instance, 1u,
            NULL, 0u, &order, 1u), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_resolve(&context), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_start(&context),
                    SALTS_COMPONENT_INTERFACE_UNAVAILABLE);
        check_equal(broken.activates, 0u);
        check_equal(broken.destroys, 1u);
        check_equal(salts_component_context_failure(&context)->phase,
                    SALTS_COMPONENT_PHASE_PROVIDE);
    }

    it("keeps two static component contexts fully independent") {
        test_provider_state left;
        test_provider_state right;
        salts_component_provider_binding left_provider;
        salts_component_provider_binding right_provider;
        salts_component_instance left_instance;
        salts_component_instance right_instance;
        size_t left_order;
        size_t right_order;
        salts_component_context left_context;
        salts_component_context right_context;
        salts_component_service left_service;
        salts_component_service right_service;
        test_log left_log = test_log_bind(NULL, NULL);
        test_log right_log = test_log_bind(NULL, NULL);

        test_provider_state_init(&left, 11, NULL);
        test_provider_state_init(&right, 22, NULL);

        left_provider = (salts_component_provider_binding){
            cmeta_component_meta(TestLogger), &left,
            NULL, NULL, &test_interfaces,
            test_logger_create, test_activate, test_deactivate
        };
        right_provider = (salts_component_provider_binding){
            cmeta_component_meta(TestLogger), &right,
            NULL, NULL, &test_interfaces,
            test_logger_create, test_activate, test_deactivate
        };

        check_equal(salts_component_context_init(
            &left_context, &left_provider, 1u, &left_instance, 1u,
            NULL, 0u, &left_order, 1u), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_init(
            &right_context, &right_provider, 1u, &right_instance, 1u,
            NULL, 0u, &right_order, 1u), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_resolve(&left_context), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_resolve(&right_context), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_start(&left_context), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_start(&right_context), SALTS_COMPONENT_OK);

        check_equal(salts_component_context_find_service(
            &left_context, test_log_interface(), &left_service), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_find_service(
            &right_context, test_log_interface(), &right_service), SALTS_COMPONENT_OK);
        check_equal(test_log_borrow_from_object(
            left_service.object, left_service.interfaces, &left_log), CMETA_OK);
        check_equal(test_log_borrow_from_object(
            right_service.object, right_service.interfaces, &right_log), CMETA_OK);
        check_equal(test_log_get(&left_log), 11);
        check_equal(test_log_get(&right_log), 22);

        check_equal(salts_component_context_stop(&left_context), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_stop(&right_context), SALTS_COMPONENT_OK);
    }

    it("rolls back prior active components exactly once on activation failure") {
        int config = 3;
        test_provider_state logger;
        test_provider_state app;
        salts_component_provider_binding providers[2];
        salts_component_instance instances[2];
        salts_component_dependency dependencies[2];
        size_t order[2];
        salts_component_context context;

        test_provider_state_init(&logger, 4, NULL);
        test_provider_state_init(&app, 0, NULL);
        app.fail_activate = true;

        providers[0] = (salts_component_provider_binding){
            cmeta_component_meta(TestLogger), &logger,
            NULL, NULL, &test_interfaces,
            test_logger_create, test_activate, test_deactivate
        };
        providers[1] = (salts_component_provider_binding){
            cmeta_component_meta(TestApp), &app,
            &cmeta_data_int, &config, &test_interfaces,
            test_app_create, test_activate, test_deactivate
        };

        check_equal(salts_component_context_init(
            &context, providers, 2u, instances, 2u,
            dependencies, 2u, order, 2u), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_resolve(&context), SALTS_COMPONENT_OK);
        check_equal(salts_component_context_start(&context),
                    SALTS_COMPONENT_ACTIVATE_FAILED);

        check_equal(logger.deactivates, 1u);
        check_equal(logger.destroys, 1u);
        check_equal(app.deactivates, 0u);
        check_equal(app.destroys, 1u);
        check_equal(context.state, SALTS_COMPONENT_CONTEXT_FAILED);
        check_equal(salts_component_context_failure(&context)->phase,
                    SALTS_COMPONENT_PHASE_ACTIVATE);
    }
}

#endif
