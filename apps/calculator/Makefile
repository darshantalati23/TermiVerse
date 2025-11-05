# Makefile for Calculator App

# Compiler
CC = gcc

# Compiler flags
CFLAGS = -g -Wall

# The target executable
TARGET = ../../bin/calculator

# The source file
SRC = calculator.c

# Default target
all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p ../../bin
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)
	@echo "Calculator app compiled successfully!"

# Clean up build files
clean:
	rm -f $(TARGET)
	rm -f *.o