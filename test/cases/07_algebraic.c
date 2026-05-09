// expect: 42
// algebraic simplification: x + 0, x * 1, 0 * y should reduce
int main() {
    int x = 42;
    int y = 99;
    return x + 0 + x * 1 - x + 0 * y;
}
