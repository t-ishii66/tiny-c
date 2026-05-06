CC      = gcc
SRCDIR  = steps/ch07/src
BUILD   = build
CFLAGS  = -Wall -g -I$(SRCDIR) -I$(BUILD)

SRCS    = $(SRCDIR)/ast.c $(SRCDIR)/codegen.c $(SRCDIR)/optimize.c $(SRCDIR)/main.c
OBJS    = $(BUILD)/ast.o $(BUILD)/codegen.o $(BUILD)/optimize.o $(BUILD)/main.o \
          $(BUILD)/parser.tab.o $(BUILD)/lex.yy.o

.PHONY: all clean test

all: tinyc

tinyc: $(OBJS)
	$(CC) -o $@ $^

$(BUILD)/parser.tab.c $(BUILD)/parser.tab.h: $(SRCDIR)/parser.y | $(BUILD)
	bison -d -o $(BUILD)/parser.tab.c $<

$(BUILD)/lex.yy.c: $(SRCDIR)/lexer.l $(BUILD)/parser.tab.h | $(BUILD)
	flex -o $@ $<

$(BUILD)/lex.yy.o: $(BUILD)/lex.yy.c
	$(CC) $(CFLAGS) -Wno-unused-function -c -o $@ $<

$(BUILD)/parser.tab.o: $(BUILD)/parser.tab.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(SRCDIR)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

test: tinyc
	@bash test/test.sh

clean:
	rm -rf $(BUILD) tinyc
