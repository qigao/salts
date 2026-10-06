#include <cmeta/container_of.h>
typedef struct record { int value; } record;
int main(void) {
    double wrong = 0;
    return cmeta_container_of_as(&wrong,record,int,value) == 0;
}
