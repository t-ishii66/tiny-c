"""Generate tree-shaped diagrams (AST, call paths) as SVG for the docs.

Usage: python3 tools/diagrams/gen_trees.py [jp|en]

Writes to docs/images/diagrams/<lang>/ (default: jp).
"""
import sys, os, unicodedata
from xml.sax.saxutils import escape

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
LANG = sys.argv[1] if len(sys.argv) > 1 else 'jp'
assert LANG in ('jp', 'en'), 'language must be jp or en'
OUT = os.path.join(ROOT, 'docs', 'images', 'diagrams', LANG)


def t(ja, en):
    return ja if LANG == 'jp' else en

FONT = "'Hiragino Sans','Hiragino Kaku Gothic ProN','Noto Sans JP','Yu Gothic',Meiryo,sans-serif"
MONO = "SFMono-Regular, Menlo, Consolas, 'Liberation Mono', monospace"

STYLE = {
    'stmt': ('#eef3fb', '#4a6fa5'),
    'op':   ('#fff1dc', '#dd8a2a'),
    'leaf': ('#f4f6f8', '#718096'),
    'nt':   ('#eef3fb', '#4a6fa5'),
    'tok':  ('#f4f6f8', '#718096'),
    'hot':  ('#fde8e8', '#c53030'),
}


def text_w(s, size):
    w = 0
    for ch in s:
        if unicodedata.east_asian_width(ch) in ('W', 'F'):
            w += size * 1.0
        else:
            w += size * 0.62
    return w


def N(label, cls='stmt', note=None, edge=None, children=()):
    return {'label': label, 'cls': cls, 'note': note, 'edge': edge, 'children': list(children)}


# ---------------------------------------------------------------- top-down tree
BOX_H = 28
GAP_X = 16


def measure(n):
    n['w'] = max(text_w(n['label'], 13) + 22, 44)
    nw = text_w(n['note'], 11) + 8 if n['note'] else 0
    ew = text_w(n['edge'], 11) + 8 if n['edge'] else 0
    own = max(n['w'], nw, ew)
    if n['children']:
        for c in n['children']:
            measure(c)
        kids = sum(c['tw'] for c in n['children']) + GAP_X * (len(n['children']) - 1)
        n['tw'] = max(own, kids)
    else:
        n['tw'] = own


def place(n, x0, y, level_h):
    n['x'] = x0 + n['tw'] / 2
    n['y'] = y
    if n['children']:
        kids = sum(c['tw'] for c in n['children']) + GAP_X * (len(n['children']) - 1)
        cx = x0 + (n['tw'] - kids) / 2
        for c in n['children']:
            place(c, cx, y + level_h, level_h)
            cx += c['tw'] + GAP_X


def depth(n):
    return 1 + max((depth(c) for c in n['children']), default=0)


def has_notes(n):
    return bool(n['note']) or any(has_notes(c) for c in n['children'])


def draw_tree(n, out):
    # edges start below the note so that the note text stays readable
    y0 = n['y'] + BOX_H + (18 if n['note'] else 0)
    for c in n['children']:
        out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#4a5568" stroke-width="1.3"/>'
                   % (n['x'], y0, c['x'], c['y']))
    for c in n['children']:
        if c['edge']:
            out.append('<text x="%.1f" y="%.1f" font-size="11" fill="#b86b14" text-anchor="middle" '
                       'stroke="#ffffff" stroke-width="4" paint-order="stroke">%s</text>'
                       % ((n['x'] + c['x']) / 2, (y0 + c['y']) / 2 + 4, escape(c['edge'])))
    fill, stroke = STYLE[n['cls']]
    out.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%d" rx="6" fill="%s" stroke="%s" stroke-width="1.5"/>'
               % (n['x'] - n['w'] / 2, n['y'], n['w'], BOX_H, fill, stroke))
    out.append('<text x="%.1f" y="%.1f" text-anchor="middle" font-family="%s" font-size="13">%s</text>'
               % (n['x'], n['y'] + 19, MONO, escape(n['label'])))
    if n['note']:
        out.append('<text x="%.1f" y="%.1f" text-anchor="middle" font-size="11" fill="#4a5568">%s</text>'
                   % (n['x'], n['y'] + BOX_H + 14, escape(n['note'])))
    for c in n['children']:
        draw_tree(c, out)


def tree_panel(root, title=None):
    """Return (width, height, draw(out, ox, oy))."""
    measure(root)
    level_h = 78 if has_notes(root) else 58
    top = 28 if title else 0
    h = top + (depth(root) - 1) * level_h + BOX_H + (18 if has_notes(root) else 4)
    w = root['tw']

    def draw(out, ox, oy):
        place(root, ox, oy + top, level_h)
        if title:
            out.append('<text x="%.1f" y="%.1f" text-anchor="middle" font-size="13" font-weight="bold" fill="#2d3748">%s</text>'
                       % (ox + w / 2, oy + 16, escape(title)))
        draw_tree(root, out)
    return w, h, draw


# ---------------------------------------------------------------- outline tree
ROW_H = 36
INDENT = 34


def outline_rows(n, d, rows):
    rows.append((n, d))
    for c in n['children']:
        outline_rows(c, d + 1, rows)


def outline_panel(root, note_x):
    rows = []
    outline_rows(root, 0, rows)
    h = len(rows) * ROW_H + 4
    for n, d in rows:
        n['w'] = text_w(n['label'], 13) + 20
    w = note_x + max((text_w(n['note'], 12) for n, d in rows if n['note']), default=0) + 10

    def draw(out, ox, oy):
        pos = {}
        for i, (n, d) in enumerate(rows):
            pos[id(n)] = (ox + d * INDENT, oy + i * ROW_H)
        # connectors
        for n, d in rows:
            px, py = pos[id(n)]
            for c in n['children']:
                cx, cy = pos[id(c)]
                out.append('<path d="M%.1f,%.1f L%.1f,%.1f L%.1f,%.1f" fill="none" stroke="#a0aec0" stroke-width="1.3"/>'
                           % (px + 12, py + BOX_H, px + 12, cy + BOX_H / 2, cx, cy + BOX_H / 2))
        for n, d in rows:
            x, y = pos[id(n)]
            fill, stroke = STYLE[n['cls']]
            out.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%d" rx="6" fill="%s" stroke="%s" stroke-width="1.5"/>'
                       % (x, y, n['w'], BOX_H, fill, stroke))
            out.append('<text x="%.1f" y="%.1f" font-family="%s" font-size="13">%s</text>'
                       % (x + 10, y + 19, MONO, escape(n['label'])))
            if n['note']:
                lx = x + n['w']
                out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#e2e8f0" stroke-width="1" stroke-dasharray="2 3"/>'
                           % (lx + 4, y + BOX_H / 2, ox + note_x - 6, y + BOX_H / 2))
                color = '#c53030' if n['cls'] == 'hot' else '#4a5568'
                out.append('<text x="%.1f" y="%.1f" font-size="12" fill="%s">%s</text>'
                           % (ox + note_x, y + 18, color, escape(n['note'])))
    return w, h, draw


# ---------------------------------------------------------------- composition
ARROW_W = 70


def compose(name, panels, arrow_labels=None, margin=20, caption=None):
    """panels laid out left to right with arrows in between."""
    widths = [p[0] for p in panels]
    heights = [p[1] for p in panels]
    content_w = sum(widths) + ARROW_W * (len(panels) - 1)
    cap_w = text_w(caption, 12) if caption else 0
    W = int(margin * 2 + max(content_w, cap_w)) + 1
    H = int(margin * 2 + max(heights) + (26 if caption else 0)) + 1
    shift = (max(content_w, cap_w) - content_w) / 2
    out = []
    out.append('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" '
               'font-family="%s" font-size="14" fill="#1a202c">' % (W, H, W, H, FONT))
    out.append('<defs><marker id="a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" '
               'orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#4a5568"/></marker></defs>')
    out.append('<rect width="%d" height="%d" fill="#ffffff"/>' % (W, H))
    x = margin + shift
    for i, (w, h, draw) in enumerate(panels):
        draw(out, x, margin)
        x += w
        if i < len(panels) - 1:
            ay = margin + 44
            out.append('<line x1="%.1f" y1="%d" x2="%.1f" y2="%d" stroke="#4a5568" stroke-width="1.6" marker-end="url(#a)"/>'
                       % (x + 12, ay, x + ARROW_W - 12, ay))
            if arrow_labels and arrow_labels[i]:
                out.append('<text x="%.1f" y="%d" text-anchor="middle" font-size="11" fill="#4a5568">%s</text>'
                           % (x + ARROW_W / 2, ay - 8, escape(arrow_labels[i])))
            x += ARROW_W
    if caption:
        out.append('<text x="%d" y="%d" font-size="12" fill="#4a5568">%s</text>' % (margin, H - margin + 4, escape(caption)))
    out.append('</svg>')
    open(os.path.join(OUT, name), 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    print('wrote', name, W, H)


# ================================================================ figures

# 1-9: the finished tree after parsing
compose('fig-1-9.svg', [tree_panel(
    N('FUNC_DEF main', children=[N('BLOCK', children=[N('RETURN', children=[N('INT_LIT 42', 'leaf')])])]))])

# 1-11: "return 42;" becomes a RETURN node
compose('fig-1-11.svg', [
    tree_panel(N('"return 42;"', 'tok'), title=t('ソース', 'Source')),
    tree_panel(N('RETURN', children=[N('INT_LIT 42', 'leaf', note=t('値: 42', 'value: 42'))]), title=t('AST のノード', 'AST node')),
])

# 2-2: left associativity of 10 - 3 - 2
compose('fig-2-2.svg', [tree_panel(
    N('add_expr', 'nt', note='"10 - 3 - 2"', children=[
        N('add_expr', 'nt', note='"10 - 3"', children=[
            N('add_expr', 'nt', note='"10"'),
            N("'-'", 'op'),
            N('mul_expr', 'nt', note='"3"'),
        ]),
        N("'-'", 'op'),
        N('mul_expr', 'nt', note='"2"'),
    ]))],
    caption=t('左側にもう一段 add_expr が伸びる → (10 - 3) - 2 = 5（左結合）', 'The extra add_expr grows on the left → (10 - 3) - 2 = 5 (left-associative)'))

# 2-4: AST of 2 + 3 * 4 and the instructions each node produces
compose('fig-2-4.svg', [tree_panel(
    N('BINARY +', 'op', note=t('最後の addl', 'the last addl'), children=[
        N('INT_LIT 2', 'leaf', note=t('movl $2, %eax（2 回目）', 'movl $2, %eax (2nd)')),
        N('BINARY *', 'op', note=t('内側の imull', 'the inner imull'), children=[
            N('INT_LIT 3', 'leaf', note='movl $3, %eax'),
            N('INT_LIT 4', 'leaf', note=t('movl $4, %eax（1 回目）', 'movl $4, %eax (1st)')),
        ]),
    ]))],
    caption=t('右の子から先に評価するので、最初に出る命令は movl $4', 'The right child is evaluated first, so the first instruction is movl $4'))

# 3-4: x = x + 5;
compose('fig-3-4.svg', [tree_panel(
    N('EXPR_STMT', children=[N('ASSIGN', 'op', children=[
        N('IDENT x', 'leaf', edge='lhs'),
        N('BINARY +', 'op', edge='rhs', children=[N('IDENT x', 'leaf'), N('INT_LIT 5', 'leaf')]),
    ])]))])

# 3-5: a = b = 7;
compose('fig-3-5.svg', [tree_panel(
    N('ASSIGN', 'op', children=[
        N('IDENT a', 'leaf', edge='lhs'),
        N('ASSIGN', 'op', edge='rhs', children=[N('IDENT b', 'leaf', edge='lhs'), N('INT_LIT 7', 'leaf', edge='rhs')]),
    ]))],
    caption=t('= は右結合。右の代入 b = 7 が先に評価され、その値 7 が a に入る', '= is right-associative: b = 7 runs first, and its value 7 goes into a'))

# 4-1: if-else
compose('fig-4-1.svg', [tree_panel(
    N('IF', children=[
        N('BINARY >', 'op', edge=t('条件', 'condition'), children=[N('IDENT x', 'leaf'), N('INT_LIT 0', 'leaf')]),
        N('EXPR_STMT', edge=t('then 部', 'then'), children=[N('ASSIGN', 'op', children=[N('IDENT y', 'leaf'), N('INT_LIT 1', 'leaf')])]),
        N('EXPR_STMT', edge=t('else 部', 'else'), children=[N('ASSIGN', 'op', children=[N('IDENT y', 'leaf'), N('INT_LIT 2', 'leaf')])]),
    ]))])

# 4-2: while
compose('fig-4-2.svg', [tree_panel(
    N('WHILE', children=[
        N('BINARY <', 'op', edge=t('条件', 'condition'), children=[N('IDENT i', 'leaf'), N('INT_LIT 10', 'leaf')]),
        N('EXPR_STMT', edge=t('本体', 'body'), children=[N('ASSIGN', 'op', children=[
            N('IDENT i', 'leaf'),
            N('BINARY +', 'op', children=[N('IDENT i', 'leaf'), N('INT_LIT 1', 'leaf')]),
        ])]),
    ]))])

# 7-2: 2 + 3 folds to 5
compose('fig-7-2.svg', [
    tree_panel(N('BINARY +', 'op', children=[N('INT_LIT 2', 'leaf'), N('INT_LIT 3', 'leaf')])),
    tree_panel(N('INT_LIT 5', 'leaf')),
], arrow_labels=[t('畳み込み', 'fold')])

# 7-3: 2 + 3 * 4 folds bottom-up to 14
compose('fig-7-3.svg', [
    tree_panel(N('BINARY +', 'op', children=[
        N('INT_LIT 2', 'leaf'),
        N('BINARY *', 'op', children=[N('INT_LIT 3', 'leaf'), N('INT_LIT 4', 'leaf')]),
    ])),
    tree_panel(N('BINARY +', 'op', children=[N('INT_LIT 2', 'leaf'), N('INT_LIT 12', 'leaf')])),
    tree_panel(N('INT_LIT 14', 'leaf')),
], arrow_labels=['3 * 4 → 12', '2 + 12 → 14'], caption=t('葉に近いところから順に、定数どうしの演算を 1 つの INT_LIT に置き換えていく', 'Starting near the leaves, each constant operation is replaced by a single INT_LIT'))

# 6-1: call path for a[i] = ...
compose('fig-6-1.svg', [outline_panel(
    N('gen_stmt(EXPR_STMT)', children=[
        N('gen_expr(NODE_ASSIGN)', 'op', note=t('入口', 'entry'), children=[
            N('gen_addr(node->lhs)', note='lhs = NODE_INDEX(a, i)', children=[
                N('case NODE_INDEX', 'hot', note=t('★ 6.2.4.3 に到達', '★ reaches 6.2.4.3'), children=[
                    N('gen_addr(IDENT a)', 'leaf', note=t('6.2.4.1（配列のベースアドレス）', '6.2.4.1 (base address of the array)')),
                    N('gen_expr(IDENT i)', 'leaf'),
                    N('imulq $4, %rax', 'hot', note=t('スケーリング！', 'scaling!')),
                    N('addq', 'leaf', note='%rax = &a[i]'),
                ]),
            ]),
            N('… store', 'leaf'),
        ]),
    ]), note_x=330)])

# 6-2: call path for *(p + i) = ...
compose('fig-6-2.svg', [outline_panel(
    N('gen_stmt(EXPR_STMT)', children=[
        N('gen_expr(NODE_ASSIGN)', 'op', note=t('入口', 'entry'), children=[
            N('gen_addr(node->lhs)', note="lhs = NODE_UNARY '*'", children=[
                N("case NODE_UNARY '*'", note=t('6.2.4.2 に到達（6.2.4.3 ではない！）', 'reaches 6.2.4.2 (not 6.2.4.3!)'), children=[
                    N('gen_expr(operand)', note="operand = NODE_BINARY '+'", children=[
                        N('case NODE_BINARY', note=t('6.2.4.3 にも 6.2.4.2 にも入らず、ただの addl', 'enters neither 6.2.4.3 nor 6.2.4.2, just an addl'), children=[
                            N('gen_expr(rhs = i)', 'leaf'),
                            N('gen_expr(lhs = p)', 'leaf'),
                            N('addl %ecx, %eax', 'hot', note=t('スケーリングなし！', 'no scaling!')),
                        ]),
                    ]),
                ]),
            ]),
            N('… store', 'leaf'),
        ]),
    ]), note_x=360)])
