// expect: 100
// constant folding: (5+5)*(3+7) should fold to 100 at compile time
int main() {
    return (5 + 5) * (3 + 7);
}
