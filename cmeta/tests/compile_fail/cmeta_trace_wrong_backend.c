#include <cmeta/trace.h>
cmeta_tracepoint(request, cmeta_field(int, status));
static void wrong_backend(int status) { (void)status; }
int main(void) { return (int)cmeta_trace_bind(request, wrong_backend); }
