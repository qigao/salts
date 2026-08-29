#include <cflow/io_communication.h>

enum {
    CFLOW_IO_READY_SUPPORTED =
        CFLOW_IO_READY_READ | CFLOW_IO_READY_WRITE |
        CFLOW_IO_READY_ERROR | CFLOW_IO_READY_HANGUP
};

bool cflow_io_endpoint_identity_valid(
    const cflow_io_endpoint_identity *identity) {
    return identity != NULL && identity->endpoint_id != 0u &&
           identity->generation != 0u;
}

bool cflow_io_control_command_valid(
    const cflow_io_control_command *command) {
    return command != NULL &&
           command->kind == CFLOW_IO_CONTROL_COMMAND_CLOSE &&
           cflow_io_endpoint_identity_valid(&command->endpoint);
}

bool cflow_io_identity_valid(const cflow_io_identity *identity) {
    return identity != NULL && identity->endpoint_id != 0u &&
           identity->request_id != 0u && identity->generation != 0u;
}

static bool readiness_events_valid(cflow_io_readiness_events events) {
    return events != 0u && (events & ~CFLOW_IO_READY_SUPPORTED) == 0u;
}

bool cflow_io_readiness_command_valid(
    const cflow_io_readiness_command *command) {
    if (command == NULL || !cflow_io_identity_valid(&command->identity))
        return false;
    switch (command->kind) {
        case CFLOW_IO_READINESS_COMMAND_WATCH:
        case CFLOW_IO_READINESS_COMMAND_MODIFY:
            return readiness_events_valid(command->interests);
        case CFLOW_IO_READINESS_COMMAND_UNWATCH:
            return command->interests == 0u;
    }
    return false;
}

bool cflow_io_readiness_event_valid(
    const cflow_io_readiness_event *event) {
    if (event == NULL || !cflow_io_identity_valid(&event->identity))
        return false;
    switch (event->kind) {
        case CFLOW_IO_READINESS_EVENT_READY:
            return readiness_events_valid(event->events) && event->error == 0;
        case CFLOW_IO_READINESS_EVENT_FAILED:
            return event->events == 0u && event->error != 0;
        case CFLOW_IO_READINESS_EVENT_CLOSED:
            return event->events == 0u && event->error == 0;
    }
    return false;
}

bool cflow_io_completion_command_valid(
    const cflow_io_completion_command *command) {
    if (command == NULL || !cflow_io_identity_valid(&command->identity))
        return false;
    switch (command->kind) {
        case CFLOW_IO_COMPLETION_COMMAND_SUBMIT:
        case CFLOW_IO_COMPLETION_COMMAND_CANCEL:
            return command->operation_id != 0u;
    }
    return false;
}

bool cflow_io_completion_event_valid(
    const cflow_io_completion_event *event) {
    if (event == NULL || !cflow_io_identity_valid(&event->identity))
        return false;
    switch (event->kind) {
        case CFLOW_IO_COMPLETION_EVENT_COMPLETED:
            return event->error == 0;
        case CFLOW_IO_COMPLETION_EVENT_EOF:
        case CFLOW_IO_COMPLETION_EVENT_CANCELLED:
            return event->transferred == 0u && event->error == 0;
        case CFLOW_IO_COMPLETION_EVENT_FAILED:
            return event->transferred == 0u && event->error != 0;
    }
    return false;
}

bool cflow_io_blocking_command_valid(
    const cflow_io_blocking_command *command) {
    if (command == NULL || !cflow_io_identity_valid(&command->identity))
        return false;
    switch (command->kind) {
        case CFLOW_IO_BLOCKING_COMMAND_EXECUTE:
        case CFLOW_IO_BLOCKING_COMMAND_CANCEL:
            return command->job_id != 0u;
    }
    return false;
}

bool cflow_io_blocking_event_valid(
    const cflow_io_blocking_event *event) {
    if (event == NULL || !cflow_io_identity_valid(&event->identity))
        return false;
    switch (event->kind) {
        case CFLOW_IO_BLOCKING_EVENT_COMPLETED:
            return event->error == 0;
        case CFLOW_IO_BLOCKING_EVENT_EOF:
        case CFLOW_IO_BLOCKING_EVENT_CANCELLED:
            return event->transferred == 0u && event->error == 0;
        case CFLOW_IO_BLOCKING_EVENT_FAILED:
            return event->transferred == 0u && event->error != 0;
    }
    return false;
}
