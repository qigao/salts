#if !defined(_WIN32)
#error "Windows private dependency fixture is Windows-only"
#endif

__declspec(dllexport) int salts_plugin_private_dependency_value(void) {
    return 42;
}
