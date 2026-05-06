// expect: 100
// inner block can shadow a function parameter
int f(int x) {
    { int x = 100; return x; }
    return 999;
}
int main() { return f(7); }
