// expect: 5
int my_strlen(char *s) {
    int n = 0;
    while (s[n] != 0) {
        n = n + 1;
    }
    return n;
}

int main() {
    char *s = "hello";
    return my_strlen(s);
}
