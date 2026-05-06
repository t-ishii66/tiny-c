// expect: 5
// after the inner block ends the inner shadow is gone; outer x is back
int main() {
    int x = 5;
    { int x = 10; }
    return x;
}
