#include <cflow/backend.h>

bool cflow_set_state_ops_valid(const cflow_set_state_ops *ops) {
    return ops && ops->open && ops->insert_if_absent && ops->close;
}

bool cflow_sequence_state_ops_valid(const cflow_sequence_state_ops *ops) {
    return ops && ops->open && ops->append && ops->stable_sort &&
           ops->range && ops->close;
}
