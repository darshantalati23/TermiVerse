# Makefile for Alarm App

# Compiler
CC = gcc

# Compiler flags
CFLAGS = -g -Wall

# The target executable
TARGET = ../../bin/alarm

# The source file
SRC = alarm.c

# Default target
all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p ../../bin
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)
	@echo "Alarm app compiled successfully!"

# Clean up build files
clean:
	rm -f $(TARGET)
	rm -f *.o