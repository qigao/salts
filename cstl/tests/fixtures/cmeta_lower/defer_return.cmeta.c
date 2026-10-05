static void cleanup_marker(int *value) {
    ++*value;
}

int main(void) {
    int marker = 0;
    defer cleanup_marker(&marker);
    return marker;
}
