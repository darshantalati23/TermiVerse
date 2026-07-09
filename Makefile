# Master Makefile for TermiVerse

# List of all directories that need building
SUBDIRS = launcher apps/snake apps/tetris apps/notes apps/chat_server apps/chat_client apps/calculator apps/alarm apps/turtlesim apps/benchmark

# Phony targets (not actual files)
.PHONY: all clean $(SUBDIRS)

# Default target: Build everything
all: $(SUBDIRS)

# Rule to build each subdirectory
$(SUBDIRS):
	@echo "Building $@"
	@$(MAKE) -C $@

# Rule to clean everything
clean:
	@echo "Cleaning all build files..."
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
	@rm -f bin/*.wav 
	@echo "Clean complete."