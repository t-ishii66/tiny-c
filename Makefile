CC = gcc
CFLAGS = -Wall -g -Isrc
LEX = flex
YACC = bison

SRCDIR = src
BUILDDIR = build

TARGET = tinyc

SRCS = $(BUILDDIR)/parser.tab.c $(BUILDDIR)/lex.yy.c $(SRCDIR)/ast.c $(SRCDIR)/codegen.c $(SRCDIR)/main.c
OBJS = $(BUILDDIR)/parser.tab.o $(BUILDDIR)/lex.yy.o $(BUILDDIR)/ast.o $(BUILDDIR)/codegen.o $(BUILDDIR)/main.o

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILDDIR)/parser.tab.c $(BUILDDIR)/parser.tab.h: $(SRCDIR)/parser.y | $(BUILDDIR)
	$(YACC) -d -o $(BUILDDIR)/parser.tab.c $<

$(BUILDDIR)/lex.yy.c: $(SRCDIR)/lexer.l $(BUILDDIR)/parser.tab.h | $(BUILDDIR)
	$(LEX) -o $@ $<

$(BUILDDIR)/parser.tab.o: $(BUILDDIR)/parser.tab.c
	$(CC) $(CFLAGS) -I$(BUILDDIR) -c -o $@ $<

$(BUILDDIR)/lex.yy.o: $(BUILDDIR)/lex.yy.c
	$(CC) $(CFLAGS) -I$(BUILDDIR) -Wno-unused-function -c -o $@ $<

$(BUILDDIR)/ast.o: $(SRCDIR)/ast.c $(SRCDIR)/ast.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/codegen.o: $(SRCDIR)/codegen.c $(SRCDIR)/codegen.h $(SRCDIR)/ast.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/main.o: $(SRCDIR)/main.c $(SRCDIR)/ast.h $(SRCDIR)/codegen.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

test: $(TARGET)
	@bash test/test.sh

clean:
	rm -rf $(BUILDDIR) $(TARGET)
