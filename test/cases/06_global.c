// expect: 99
int g;

int set(int v) {
    g = v;
    return 0;
}

int main() {
    set(99);
    return g;
}
