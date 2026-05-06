// expect: 3
// 3-level nesting; the innermost name wins
int main() {
    int x = 1;
    { int x = 2;
      { int x = 3; return x; }
    }
    return 99;
}
