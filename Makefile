CC = gcc
CFLAGS = -Wall -Wextra -std=c99 $(shell pkg-config --cflags gtk+-3.0 gtksourceview-3.0 glib-2.0)
LDFLAGS = $(shell pkg-config --libs gtk+-3.0 gtksourceview-3.0 glib-2.0) -lm

# Targets
IDE = cryptic_ide
INTERPRETER = cride_interpreter

# Source files
IDE_SOURCES = modern_ide.c ccrp.c
IDE_OBJECTS = $(IDE_SOURCES:.c=.o)

INTERPRETER_SOURCES = cride_interpreter.c ccrp.c
INTERPRETER_OBJECTS = $(INTERPRETER_SOURCES:.c=.o)

# Default target
all: $(IDE) $(INTERPRETER)

# Build modern IDE
$(IDE): $(IDE_OBJECTS)
	$(CC) $(IDE_OBJECTS) -o $(IDE) $(LDFLAGS)

# Build interpreter
$(INTERPRETER): $(INTERPRETER_OBJECTS)
	$(CC) $(INTERPRETER_OBJECTS) -o $(INTERPRETER) $(LDFLAGS)

# Compile source files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -f $(IDE) $(INTERPRETER) *.o

# Install language file
install-lang:
	mkdir -p ~/.local/share/gtksourceview-3.0/language-specs/
	cp ccrp.lang ~/.local/share/gtksourceview-3.0/language-specs/

# Run IDE
run: $(IDE)
	./$(IDE)

# Run interpreter
test: $(INTERPRETER)
	./$(INTERPRETER) test.crp

.PHONY: all clean install-lang run test