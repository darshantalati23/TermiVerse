# Makefile for TermiVerse Launcher

# Compiler
CC = gcc

# Compiler flags
# -g: Add debug info
# -Wall: Turn on all warnings
CFLAGS = -g -Wall

# The target executable
TARGET = ../bin/launcher

# The source file
SRC = launcher.c

# Default target
all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p ../bin  # Ensure the bin directory exists
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)
	@echo "Launcher compiled successfully!"

# Clean up build files
clean:
	rm -f $(TARGET)
	rm -f *.o