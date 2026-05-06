// expect: 1
int main() {
    int a = 3;
    int b = 5;
    if (a == 3) {
        if (b != 4) {
            if (a <= b) {
                if (b >= a) {
                    return 1;
                }
            }
        }
    }
    return 0;
}
