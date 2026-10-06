#include <cmeta/container_of.h>
typedef struct record { int value; } record;
int main(void) {
    record object = {0};
    return cmeta_container_of_as(&object.value,record,double,value) == &object;
}
