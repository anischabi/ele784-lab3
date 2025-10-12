# Makefile for test_app

CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = test_app
SRC = test_app.c
BIN_DIR = ../../bin
OUT = $(BIN_DIR)/$(TARGET)

all: $(OUT)

$(OUT): $(SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

# Ensure the bin directory exists
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -f $(OUT)

.PHONY: all clean