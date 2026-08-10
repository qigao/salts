#include "cxfixture.h"

struct _cxml_g_runner _g_runner;

extern void suite_cxxpath(void);

static void xpath_suite(void)
{
    suite_cxxpath();
}

int main(void)
{
    int status;
    cxml_cfg_enable_fancy_printing(0);
    cxml_cfg_show_warnings(0);
    cxml_cfg_enable_debugging(0);
    set_data_path();
    CXML_TEST_RUNNER(&status, 1, xpath_suite)
    free_data_path();
    return status;
}
