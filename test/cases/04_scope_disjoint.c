// expect: 5
// disjoint sibling blocks may reuse the same name
int main() {
    { int x = 1; }
    { int x = 2; return x + 3; }
    return 99;
}
