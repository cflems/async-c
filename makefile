PROD_DIR = src/main
TEST_DIR = src/test

HEADERS := $(wildcard $(PROD_DIR)/*.h)
PROD_TARGET := build/asyncc.so
PROD_SRC := $(wildcard $(PROD_DIR)/*.c)
TEST_TARGET := build/test.out
TEST_SRC := $(wildcard $(TEST_DIR)/*.c)

CC = gcc
CFLAGS = -I $(PROD_DIR) -Wall -Werror -Wextra -std=c99 -g
PROD_CFLAGS = -shared
TEST_CFLAGS = -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-pointer-to-int-cast
all: main test
main: $(PROD_TARGET)
test: $(TEST_TARGET)

$(PROD_TARGET): $(HEADERS) $(PROD_SRC)
	$(CC) $(CFLAGS) $(PROD_CFLAGS) $(PROD_SRC) -o $(PROD_TARGET)

$(TEST_TARGET): $(HEADERS) $(PROD_SRC) $(TEST_SRC)
	$(CC) $(CFLAGS) $(TEST_CFLAGS) $(PROD_SRC) $(TEST_SRC) -o $(TEST_TARGET)
