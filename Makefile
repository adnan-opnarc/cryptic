CC = gcc
CFLAGS = -Wall -Wextra -std=c99 $(shell pkg-config --cflags gtk+-3.0 gtksourceview-3.0 glib-2.0)
LDFLAGS = $(shell pkg-config --libs gtk+-3.0 gtksourceview-3.0 glib-2.0) -lm

# Targets
IDE = cryptic_ide
INTERPRETER = cride_interpreter
VERSION ?= 0.18
APPDIR = pkg/AppDir
DEBROOT = pkg/deb/cryptic-ide

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
	rm -rf $(IDE) $(INTERPRETER) *.o pkg

# Install language file (GtkSourceView)
install-lang:
	mkdir -p ~/.local/share/gtksourceview-3.0/language-specs/
	cp ccrp.lang ~/.local/share/gtksourceview-3.0/language-specs/

# Register .crp as Cryptic source (text/x-ccrp) in user MIME db
install-mime:
	mkdir -p ~/.local/share/mime/packages
	@printf '%s\n' \
		'<?xml version="1.0" encoding="UTF-8"?>' \
		'<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">' \
		'  <mime-type type="text/x-ccrp">' \
		'    <comment>Cryptic CCRP source</comment>' \
		'    <glob pattern="*.crp"/>' \
		'  </mime-type>' \
		'</mime-info>' > ~/.local/share/mime/packages/cryptic-ccrp.xml
	update-mime-database ~/.local/share/mime

# Run IDE
run: $(IDE)
	./$(IDE)

# Run interpreter
test: $(INTERPRETER)
	./$(INTERPRETER) test.crp

# ---------------- Packaging ----------------
# Create AppDir layout for AppImage
appdir: $(IDE) $(INTERPRETER)
	mkdir -p $(APPDIR)/usr/bin
	mkdir -p $(APPDIR)/usr/share/applications
	mkdir -p $(APPDIR)/usr/share/icons/hicolor/256x256/apps
	cp $(IDE) $(APPDIR)/usr/bin/
	cp $(INTERPRETER) $(APPDIR)/usr/bin/
	# Desktop entry
	@{ \
	  printf '%s\n' '[Desktop Entry]'; \
	  printf '%s\n' 'Name=Cryptic IDE'; \
	  printf '%s\n' 'Exec=cryptic_ide'; \
	  printf '%s\n' 'Icon=cryptic-ide'; \
	  printf '%s\n' 'Type=Application'; \
	  printf '%s\n' 'Categories=Development;IDE;'; \
	  printf '%s\n' 'MimeType=text/x-ccrp;'; \
	} > $(APPDIR)/usr/share/applications/cryptic-ide.desktop
	cp $(APPDIR)/usr/share/applications/cryptic-ide.desktop $(APPDIR)/cryptic-ide.desktop
	# Icon: use project-root cryptic-ide.png if present; else write placeholder
	@if [ -f logo.png ]; then \
	  cp logo.png $(APPDIR)/usr/share/icons/hicolor/256x256/apps/cryptic-ide.png; \
	  cp logo.png $(APPDIR)/cryptic-ide.png; \
	else \
	  printf '%s\n' 'PNG PLACEHOLDER' > $(APPDIR)/usr/share/icons/hicolor/256x256/apps/cryptic-ide.png; \
	  cp $(APPDIR)/usr/share/icons/hicolor/256x256/apps/cryptic-ide.png $(APPDIR)/cryptic-ide.png; \
	fi
	# AppRun launcher
	@{ \
	  printf '%s\n' '#!/bin/sh'; \
	  printf '%s\n' 'exec "$$APPDIR"/usr/bin/cryptic_ide "$$@"'; \
	} > $(APPDIR)/AppRun
	chmod +x $(APPDIR)/AppRun

# Build AppImage using appimagetool (must be installed in PATH)
appimage: appdir
	appimagetool $(APPDIR) pkg/CrypticIDE-$(VERSION)-x86_64.AppImage
	@echo "AppImage created at pkg/CrypticIDE-$(VERSION)-x86_64.AppImage"

# Build a simple .deb (user scope)
deb: $(IDE)
	rm -rf $(DEBROOT)
	mkdir -p $(DEBROOT)/DEBIAN
	mkdir -p $(DEBROOT)/usr/bin
	mkdir -p $(DEBROOT)/usr/share/applications
	mkdir -p $(DEBROOT)/usr/share/icons/hicolor/256x256/apps
	# Control file
	@printf '%s\n' \
	'Package: cryptic-ide' \
	'Version: $(VERSION)' \
	'Section: utils' \
	'Priority: optional' \
	'Architecture: amd64' \
	'Maintainer: Unknown <unknown@example.com>' \
	'Description: Cryptic IDE for CCRP language' > $(DEBROOT)/DEBIAN/control
	cp $(IDE) $(DEBROOT)/usr/bin/
	# Desktop entry
	@mkdir -p $(DEBROOT)/usr/share/applications
	@{ \
	  printf '%s\n' '[Desktop Entry]'; \
	  printf '%s\n' 'Name=Cryptic IDE'; \
	  printf '%s\n' 'Exec=cryptic_ide'; \
	  printf '%s\n' 'Icon=cryptic-ide'; \
	  printf '%s\n' 'Type=Application'; \
	  printf '%s\n' 'Categories=Development;IDE;'; \
	  printf '%s\n' 'MimeType=text/x-ccrp;'; \
	} > $(DEBROOT)/usr/share/applications/cryptic-ide.desktop
	# Placeholder icon
	@printf '%s\n' 'PNG PLACEHOLDER' > $(DEBROOT)/usr/share/icons/hicolor/256x256/apps/cryptic-ide.png
	dpkg-deb --build $(DEBROOT) pkg/cryptic-ide_$(VERSION)_amd64.deb
	@echo "Debian package created at pkg/cryptic-ide_$(VERSION)_amd64.deb"

.PHONY: all clean install-lang install-mime run test appdir appimage deb