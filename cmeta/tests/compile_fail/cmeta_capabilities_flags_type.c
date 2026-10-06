#include <cmeta/flags.h>
cmeta_flags(FirstFlags, "test.FirstFlags", cmeta_flag(Read, 1u, "read"));
cmeta_flags(OtherFlags, "test.OtherFlags", cmeta_flag(Read, 1u, "read"));
int main(void) {
    FirstFlags result = {0};
    return FirstFlags_or(FirstFlags_Read, OtherFlags_Read, &result);
}
