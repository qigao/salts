#include <cflow/io_communication.h>

#include "tinytest.h"

spec("CFlow IO communication contracts") {
    it("addresses endpoint close outside request command models") {
        const cflow_io_endpoint_identity endpoint = {7u, 3u};
        const cflow_io_endpoint_identity missing_generation = {7u, 0u};
        const cflow_io_control_command close = {
            CFLOW_IO_CONTROL_COMMAND_CLOSE, {7u, 3u}};
        const cflow_io_control_command unknown = {
            (cflow_io_control_command_kind)99, {7u, 3u}};

        check_true(cflow_io_endpoint_identity_valid(&endpoint));
        check_false(cflow_io_endpoint_identity_valid(&missing_generation));
        check_false(cflow_io_endpoint_identity_valid(NULL));
        check_true(cflow_io_control_command_valid(&close));
        check_false(cflow_io_control_command_valid(&unknown));
        check_false(cflow_io_control_command_valid(NULL));
    }

    it("accepts identities only when every reuse boundary is explicit") {
        const cflow_io_identity valid = {7u, 11u, 3u};
        const cflow_io_identity no_endpoint = {0u, 11u, 3u};
        const cflow_io_identity no_request = {7u, 0u, 3u};
        const cflow_io_identity no_generation = {7u, 11u, 0u};

        check_true(cflow_io_identity_valid(&valid));
        check_false(cflow_io_identity_valid(&no_endpoint));
        check_false(cflow_io_identity_valid(&no_request));
        check_false(cflow_io_identity_valid(&no_generation));
        check_false(cflow_io_identity_valid(NULL));
    }

    it("keeps readiness commands and observations readiness-specific") {
        const cflow_io_readiness_command watch = {
            CFLOW_IO_READINESS_COMMAND_WATCH, {7u, 11u, 3u},
            CFLOW_IO_READY_READ | CFLOW_IO_READY_WRITE};
        const cflow_io_readiness_command invalid_mask = {
            CFLOW_IO_READINESS_COMMAND_WATCH, {7u, 11u, 3u}, 1u << 31u};
        const cflow_io_readiness_event ready = {
            CFLOW_IO_READINESS_EVENT_READY, {7u, 11u, 3u},
            CFLOW_IO_READY_READ, 0};
        const cflow_io_readiness_event failed_without_error = {
            CFLOW_IO_READINESS_EVENT_FAILED, {7u, 11u, 3u}, 0u, 0};
        const cflow_io_readiness_event unknown = {
            (cflow_io_readiness_event_kind)99, {7u, 11u, 3u}, 0u, 0};

        check_true(cflow_io_readiness_command_valid(&watch));
        check_false(cflow_io_readiness_command_valid(&invalid_mask));
        check_true(cflow_io_readiness_event_valid(&ready));
        check_false(cflow_io_readiness_event_valid(
            &failed_without_error));
        check_false(cflow_io_readiness_event_valid(&unknown));
        check_false(cflow_io_readiness_command_valid(NULL));
        check_false(cflow_io_readiness_event_valid(NULL));
    }

    it("requires completion events to encode one terminal result") {
        const cflow_io_completion_command submit = {
            CFLOW_IO_COMPLETION_COMMAND_SUBMIT, {2u, 5u, 1u}, 19u};
        const cflow_io_completion_event completed = {
            CFLOW_IO_COMPLETION_EVENT_COMPLETED, {2u, 5u, 1u}, 4096u, 0};
        const cflow_io_completion_event eof = {
            CFLOW_IO_COMPLETION_EVENT_EOF, {2u, 5u, 1u}, 0u, 0};
        const cflow_io_completion_event eof_with_bytes = {
            CFLOW_IO_COMPLETION_EVENT_EOF, {2u, 5u, 1u}, 1u, 0};
        const cflow_io_completion_event success_with_error = {
            CFLOW_IO_COMPLETION_EVENT_COMPLETED, {2u, 5u, 1u}, 4096u, -5};
        const cflow_io_completion_event failed_without_error = {
            CFLOW_IO_COMPLETION_EVENT_FAILED, {2u, 5u, 1u}, 0u, 0};
        const cflow_io_completion_command cancel = {
            CFLOW_IO_COMPLETION_COMMAND_CANCEL, {2u, 5u, 1u}, 19u};
        const cflow_io_completion_command submit_without_operation = {
            CFLOW_IO_COMPLETION_COMMAND_SUBMIT, {2u, 5u, 1u}, 0u};

        check_true(cflow_io_completion_command_valid(&submit));
        check_true(cflow_io_completion_event_valid(&completed));
        check_true(cflow_io_completion_event_valid(&eof));
        check_false(cflow_io_completion_event_valid(&eof_with_bytes));
        check_false(cflow_io_completion_event_valid(&success_with_error));
        check_false(cflow_io_completion_event_valid(
            &failed_without_error));
        check_true(cflow_io_completion_command_valid(&cancel));
        check_false(cflow_io_completion_command_valid(
            &submit_without_operation));
        check_false(cflow_io_completion_command_valid(NULL));
        check_false(cflow_io_completion_event_valid(NULL));
    }

    it("keeps blocking jobs distinct from native completion operations") {
        const cflow_io_blocking_command execute = {
            CFLOW_IO_BLOCKING_COMMAND_EXECUTE, {4u, 9u, 2u}, 23u};
        const cflow_io_blocking_event cancelled = {
            CFLOW_IO_BLOCKING_EVENT_CANCELLED, {4u, 9u, 2u}, 0u, 0};
        const cflow_io_blocking_event eof = {
            CFLOW_IO_BLOCKING_EVENT_EOF, {4u, 9u, 2u}, 0u, 0};
        const cflow_io_blocking_event eof_with_error = {
            CFLOW_IO_BLOCKING_EVENT_EOF, {4u, 9u, 2u}, 0u, -17};
        const cflow_io_blocking_event cancelled_with_bytes = {
            CFLOW_IO_BLOCKING_EVENT_CANCELLED, {4u, 9u, 2u}, 1u, 0};
        const cflow_io_blocking_event failed = {
            CFLOW_IO_BLOCKING_EVENT_FAILED, {4u, 9u, 2u}, 0u, -17};
        const cflow_io_blocking_command execute_without_job = {
            CFLOW_IO_BLOCKING_COMMAND_EXECUTE, {4u, 9u, 2u}, 0u};

        check_true(cflow_io_blocking_command_valid(&execute));
        check_true(cflow_io_blocking_event_valid(&eof));
        check_false(cflow_io_blocking_event_valid(&eof_with_error));
        check_true(cflow_io_blocking_event_valid(&cancelled));
        check_false(cflow_io_blocking_event_valid(
            &cancelled_with_bytes));
        check_true(cflow_io_blocking_event_valid(&failed));
        check_false(cflow_io_blocking_command_valid(&execute_without_job));
        check_false(cflow_io_blocking_command_valid(NULL));
        check_false(cflow_io_blocking_event_valid(NULL));
    }
}
