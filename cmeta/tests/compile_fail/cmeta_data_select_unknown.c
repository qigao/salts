#include <cmeta/data_select.h>
int main(void) {
    void *unknown = 0;
    (void)cmeta_data_of(unknown);
    return 0;
}
