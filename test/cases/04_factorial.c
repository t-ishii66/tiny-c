// expect: 120
int main() {
    int n = 5;
    int r = 1;
    while (n > 0) {
        r = r * n;
        n = n - 1;
    }
    return r;
}
