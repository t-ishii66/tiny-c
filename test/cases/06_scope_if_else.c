// expect: 7
// if-then and else bodies are disjoint scopes; same name allowed in each
int main() {
    int c = 0;
    if (c) { int x = 100; return x; }
    else { int x = 7; return x; }
    return 99;
}
