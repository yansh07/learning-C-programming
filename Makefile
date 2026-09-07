.PHONY: all clean run help

# Compiler and flags
CC ?= gcc
CFLAGS := -Wall -Wextra -Werror -O2 -g
LDFLAGS :=
LDLIBS := -lreadline

# Project structure
SRC_DIR := src
BIN_DIR := bin
OBJ_DIR := obj

# Build targets
EXECUTABLE := $(BIN_DIR)/abyss-shell
SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))

# Default target
all: $(EXECUTABLE)

# Create directories
$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@

# Compile object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Link executable
$(EXECUTABLE): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)
	@echo "✓ Build successful: $(EXECUTABLE)"

# Run the shell
run: $(EXECUTABLE)
	./$(EXECUTABLE)

# Clean build artifacts
clean:
	@rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "✓ Clean complete"

# Display help
help:
	@echo "Abyss Shell - Build Targets:"
	@echo "  make           - Build the executable"
	@echo "  make run       - Build and run the shell"
	@echo "  make clean     - Remove build artifacts"
	@echo "  make help      - Show this help message"