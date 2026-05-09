// expect: 10
// inner block shadows outer; return picks the inner one
int main() {
    int x = 5;
    { int x = 10; return x; }
}
