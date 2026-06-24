# Makefile for TurtleSim App

# Compiler
CC = gcc

# Compiler flags
CFLAGS = -g -Wall

# The target executable
TARGET = ../../bin/turtlesim

# The source file
SRC = turtlesim.c

# Default target
all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p ../../bin
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) -lm
	@echo "TurtleSim app compiled successfully!"

# Clean up build files
clean:
	rm -f $(TARGET)
	rm -f *.o