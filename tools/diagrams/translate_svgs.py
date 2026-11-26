"""Make English versions of the hand-drawn diagrams in docs/images/diagrams/jp/.

Each entry maps an exact source snippet to its English replacement.  Every
snippet must be found, so a typo or a changed figure fails loudly instead of
leaving Japanese behind.

Usage: python3 tools/diagrams/translate_svgs.py
"""
import re, sys, os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SRC = os.path.join(ROOT, 'docs', 'images', 'diagrams', 'jp')
DST = os.path.join(ROOT, 'docs', 'images', 'diagrams', 'en')
C = '<tspan class="code">'

T = {
'fig-1-1': [
    ('>ソースコード<', '>Source code<'),
    ('>解析<', '>parse<'),
    ('>木 (AST)<', '>Tree (AST)<'),
    ('>アセンブリ<', '>Assembly<'),
],
'fig-1-2': [
    ('>人間が書いた tiny-c ソース<', '>tiny-c source written by a human<'),
    ('>flex が C コードを生成<', '>flex generates C code<'),
    ('>文字列をトークンに分解する<', '>splits the text into tokens<'),
    ('>bison が C コードを生成<', '>bison generates C code<'),
    ('>トークン列から木を組み立てる<', '>builds a tree from the tokens<'),
    ('>途中で ast.c のコンストラクタを呼ぶ<', '>calling ast.c constructors along the way<'),
    ('>Node 木<', '>Node tree<'),
    ('>メモリ上の AST<', '>the AST in memory<'),
    ('>木を辿ってアセンブリを出力<', '>walks the tree and emits assembly<'),
    ('>x86 アセンブリ<', '>x86 assembly<'),
],
'fig-1-3': [
    ('>文字列<', '>Text<'),
    ('>字句解析器<', '>lexer<'),
    ('>トークン列<', '>Tokens<'),
],
'fig-1-4': [
    ('>私たちが書く<', '>we write this<'),
    ('>字句解析器の C コード<', '>C code for the lexer<'),
],
'fig-1-5': [
    ('>トークン列<', '>Tokens<'),
    ('>構文解析器<', '>parser<'),
    ('<text class="tag" x="400" y="364">括弧や ' + C + ';</tspan> は木には残らない</text>',
     '<text class="tag" x="320" y="364" text-anchor="middle">Parentheses and ' + C + ';</tspan> are dropped from the tree</text>'),
],
'fig-1-6': [
    ('viewBox="0 0 640 170" width="640"', 'viewBox="0 0 680 170" width="680"'),
    ('<rect width="640" height="170"', '<rect width="680" height="170"'),
    ('>私たちが書く<', '>we write this<'),
    ('>構文解析器の<', '>C code for<'),
    ('>C コード<', '>the parser<'),
    ('>トークン定義の<', '>header with<'),
    ('>ヘッダ<', '>token definitions<'),
],
'fig-1-7': [
    ('>トークン<', '>Token<'),
    ('>expr ルールのアクション<', '>action of the expr rule<'),
    ('>ノード<', '>Node<'),
],
'fig-1-8': [
    ('>3 つがそろった<', '>All three are here<'),
    ('>INT_LIT(42) ノード<', '>INT_LIT(42) node<'),
    ('>stmt ルールのアクション<', '>action of the stmt rule<'),
    ('>RETURN ノード<', '>RETURN node<'),
    ('</tspan> は捨てられる<', '</tspan> is thrown away<'),
],
'fig-1-10': [
    ('>入力ファイル<', '>Input file<'),
    ('>tiny-c で書かれたソース<', '>source written in tiny-c<'),
    ('>' + C + 'FILE *</tspan> 型のグローバル変数<', '>global variable of type ' + C + 'FILE *</tspan><'),
    ('>flex 生成<', '>from flex<'),
    ('>1 回呼ぶと 1 トークン返す<', '>returns one token per call<'),
    ('>bison 生成<', '>from bison<'),
    ('>yylex を内部で呼びながら木を作る<', '>builds the tree, calling yylex inside<'),
    ('>グローバル変数。AST の根が入る<', '>global variable that holds the AST root<'),
    ('>関数<', '>function<'),
    ('>グローバル変数<', '>global variable<'),
    # "function" is wider than 関数; move the second legend entry right
    ('<rect class="var" x="540" y="490"', '<rect class="var" x="566" y="490"'),
    ('<text class="tag" x="568" y="501">', '<text class="tag" x="594" y="501">'),
],
'fig-1-12': [
    ('>グローバル変数<', '>global variable<'),
    ('>● から出る矢印 = ポインタ<', '>arrow from ● = pointer<'),
    ('>（そのフィールドが指すノード）<', '>(to the node that field points to)<'),
],
'fig-1-13': [
    ('>64 ビット全体<', '>all 64 bits<'),
    ('>下位 32 ビット<', '>low 32 bits<'),
    ('>下位 16 ビット<', '>low 16 bits<'),
    ('>同じレジスタの下位部分に、別の名前が付いている（どれかに書けば、重なる部分も変わる）<',
     '>The lower parts of one register have their own names (writing one changes the overlapping parts too)<'),
],
'fig-1-14': [
    ('>高アドレス<', '>high address<'),
    ('>低アドレス<', '>low address<'),
    ('</tspan> の ' + C + 'rbp</tspan>', '</tspan>\'s ' + C + 'rbp</tspan>'),
    ('>（空き）<', '>(free)<'),
    (C + 'push</tspan> 前の ' + C + '%rsp</tspan>', C + '%rsp</tspan> before ' + C + 'push</tspan>'),
    (C + 'push</tspan> 後の ' + C + '%rsp</tspan>', C + '%rsp</tspan> after ' + C + 'push</tspan>'),
    ('>直前のスタック先端<', '>the previous top of stack<'),
    ('>8 バイト下に動いた<', '>moved down by 8 bytes<'),
    (C + 'pushq %rbp</tspan> は ' + C + '%rsp</tspan> を 8 減らしてから、その位置に値を書く',
     C + 'pushq %rbp</tspan> subtracts 8 from ' + C + '%rsp</tspan>, then writes the value there'),
],
'fig-1-15': [
    ('>入力 ' + C, '>Input ' + C),
    ('>出力 ' + C, '>Output ' + C),
    ('>呼び出す<', '>calls<'),
],
'fig-1-16': [
    ('>ソース（src/）<', '>Sources (src/)<'),
    ('>生成されたコード（build/）<', '>Generated code (build/)<'),
    ('>オブジェクト（gcc -c）<', '>Objects (gcc -c)<'),
    ('>実行ファイル<', '>Executable<'),
    ('>gcc（リンク）<', '>gcc (link)<'),
],
'fig-2-1': [
    ('>解釈A<', '>Reading A<'),
    ('>解釈B<', '>Reading B<'),
    ('<text class="note code" x="100" y="28">', '<text class="note code" x="148" y="28">'),
    ('<text class="note code" x="460" y="28">', '<text class="note code" x="508" y="28">'),
    ('>掛け算が木の下にあるので先に計算される<', '>Multiplication is lower, so it runs first<'),
    ('>（C の規則どおり）<', '>(this is what C requires)<'),
    ('>足し算が木の下にあるので先に計算される<', '>Addition is lower, so it runs first<'),
    ('>（素朴な文法だと、こちらも作れてしまう）<', '>(a naive grammar can build this too)<'),
],
'fig-2-3': [
    ('>時刻 ', '>Time '),
    ('>（開始）<', '>(start)<'),
    ('>空<', '>empty<'),
    ('>内部計算で使う<', '>used by the operation<'),
],
'fig-3-1': [
    ('>高アドレス<', '>high address<'),
    ('>低アドレス<', '>low address<'),
    ('>呼び出し元の戻り番地<', '>caller\'s return address<'),
    ('>保存された呼び出し元の ' + C + '%rbp</tspan><', '>caller\'s saved ' + C + '%rbp</tspan><'),
    ('>1 番目の ' + C + 'subq $8</tspan> で確保<', '>allocated by the 1st ' + C + 'subq $8</tspan><'),
    ('>2 番目の ' + C + 'subq $8</tspan> で確保<', '>allocated by the 2nd ' + C + 'subq $8</tspan><'),
    ('>（空き）<', '>(free)<'),
],
'fig-3-2': [
    ('>高アドレス<', '>high address<'),
    ('>低アドレス<', '>low address<'),
    ('>呼び出し元のフレーム<', '>caller\'s frame<'),
    ('>戻り番地<', '>return address<'),
    ('>' + C + 'call</tspan> 命令で積まれた<', '>pushed by the ' + C + 'call</tspan> instruction<'),
    ('>（' + C + 'call</tspan> 直後は ' + C + '%rsp</tspan> がここを指す）<',
     '>(right after ' + C + 'call</tspan>, ' + C + '%rsp</tspan> points here)<'),
    ('>旧 ' + C + '%rbp</tspan><', '>old ' + C + '%rbp</tspan><'),
    ('>' + C + 'pushq %rbp</tspan> で積まれた<', '>pushed by ' + C + 'pushq %rbp</tspan><'),
    ('>（続く ' + C + 'movq %rsp, %rbp</tspan> で ' + C + '%rbp</tspan> がここを指す）<',
     '>(then ' + C + 'movq %rsp, %rbp</tspan> makes ' + C + '%rbp</tspan> point here)<'),
    ('>（空きスタック）<', '>(free stack)<'),
    ('>（この時点では<', '>(same address<'),
    ('>同じ番地）<', '>for now)<'),
    ('<text class="note" x="88" y="188">', '<text class="note" x="84" y="188">'),
    ('<text class="note" x="88" y="204">', '<text class="note" x="84" y="204">'),
],
'fig-3-3': [
    ('>旧 ' + C + '%rbp</tspan><', '>old ' + C + '%rbp</tspan><'),
    ('</tspan>（1 つ目）<', '</tspan> (1st)<'),
    ('</tspan>（2 つ目）<', '</tspan> (2nd)<'),
    ('</tspan>（3 つ目）<', '</tspan> (3rd)<'),
    ('>変数 1 つにつき 8 バイトずつ、' + C + '%rbp</tspan> から下へ並ぶ<',
     '>8 bytes per variable, going down from ' + C + '%rbp</tspan><'),
],
'fig-5-1': [
    ('>高アドレス<', '>high address<'),
    ('>低アドレス<', '>low address<'),
    ('>この関数の<', '>this function\'s<'),
    ('>フレーム<', '>frame<'),
    ('>戻り番地<', '>return address<'),
    ('>caller の ' + C + 'call</tspan> が積んだ<', '>pushed by the caller\'s ' + C + 'call</tspan><'),
    ('>旧 ' + C + '%rbp</tspan><', '>old ' + C + '%rbp</tspan><'),
    ('>自分の ' + C + 'pushq %rbp</tspan> が積んだ<', '>pushed by our own ' + C + 'pushq %rbp</tspan><'),
    ('>続く ' + C + 'movq %rsp, %rbp</tspan> で ' + C + '%rbp</tspan> がここを指す<',
     '>then ' + C + 'movq %rsp, %rbp</tspan> makes ' + C + '%rbp</tspan> point here<'),
    ('</tspan>（= ' + C + '%edi</tspan>）<', '</tspan> (= ' + C + '%edi</tspan>)<'),
    ('>パラメータの spill<', '>parameter spill<'),
    ('>（使われない）<', '>(unused)<'),
    ('>16-aligned のためのパディング<', '>padding for 16-byte alignment<'),
    ('>' + C + 'subq $16</tspan> で ' + C + '%rsp</tspan> がここを指す<',
     '>after ' + C + 'subq $16</tspan>, ' + C + '%rsp</tspan> points here<'),
    ('>一時値の push / pop で<', '>grows and shrinks with<'),
    ('>伸び縮みする<', '>temporary push / pop<'),
],
'fig-5-2': [
    ('>高アドレス<', '>high address<'),
    ('>低アドレス<', '>low address<'),
    ('>main の rbp<', '>main\'s rbp<'),
    ('>rbp の保存<', '>saved rbp<'),
    ('>' + C + 'rip</tspan> = 戻り番地（どこへ戻るか）<', '>' + C + 'rip</tspan> = return address (where to go back)<'),
    ('>どの ' + C + 'n</tspan> も、自分のフレームの ' + C + '-8(%rbp)</tspan><',
     '>each ' + C + 'n</tspan> is ' + C + '-8(%rbp)</tspan> of its own frame<'),
],
'fig-7-1': [
    ('>ソース<', '>source<'),
    ('>字句解析<', '>lexing<'),
    ('>構文解析<', '>parsing<'),
    ('>AST 最適化<', '>AST optimizer<'),
    ('>ピープホール<', '>peephole<'),
    ('>出力 ' + C, '>output ' + C),
    ('>ch07 で追加<', '>added in ch07<'),
    ('>単一パス化<', '>single pass<'),
    ('>（バックパッチ）<', '>(backpatching)<'),
],
'fig-7-4': [
    ('>メモリバッファ<', '>memory buffer<'),
    ('>7.2 節で導入<', '>introduced in Section 7.2<'),
    ('>この節で追加<', '>added in this section<'),
],
}

# frame labels in fig-5-2 need the words reordered
REGEX = {
'fig-5-2': [(r'>(<tspan class="code">fact\(\d\)</tspan>) のフレーム<', r'>frame of \1<')],
}

JP = re.compile(r'[ぁ-んァ-ヶ一-龠ー（）「」、。！]')
os.makedirs(DST, exist_ok=True)
for name in sorted(os.listdir(SRC)):
    stem = name[:-4]
    s = open(os.path.join(SRC, name), encoding='utf-8').read()
    for old, new in T.get(stem, []):
        assert old in s, (stem, old)
        s = s.replace(old, new)
    for pat, rep in REGEX.get(stem, []):
        s, n = re.subn(pat, rep, s)
        assert n, (stem, pat)
    # tree figures are regenerated by gen_trees.py; copy only language-free files here
    left = [l.strip() for l in s.split('\n') if JP.search(l) and '<text' in l]
    if stem in T or stem in REGEX or not left:
        assert not left, (stem, left[:3])
        open(os.path.join(DST, name), 'w', encoding='utf-8').write(s)
        print('en', name)
