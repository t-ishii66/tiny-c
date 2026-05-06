// expect: 17
int main() {
    int x = 5;
    int *p = &x;
    *p = 17;
    return x;
}
