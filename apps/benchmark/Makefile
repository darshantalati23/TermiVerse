# Makefile for Benchmark Tool

CC = gcc
CFLAGS = -Wall -O2 -pthread
LDFLAGS = -pthread

TARGET = ../../bin/ipc_bench
SRC = ipc_bench.c

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p ../../bin
	@$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)
	@echo "Benchmark tool compiled successfully!"

clean:
	@rm -f $(TARGET)
	@echo "Benchmark tool cleaned."
