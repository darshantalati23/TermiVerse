# Makefile for Notes App

# Compiler
CC = gcc

# Compiler flags
# -g: Add debug info
# -Wall: Turn on all warnings
CFLAGS = -g -Wall

# The target executable
TARGET = ../../bin/notes

# The source file
SRC = notes.c

# Default target
all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p ../../bin
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)
	@echo "Notes app compiled successfully!"

# Clean up build files
clean:
	rm -f $(TARGET)
	rm -f *.o