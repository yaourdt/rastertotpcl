
# TPCL Printer Application main Makefile

# Directories
SRCDIR = src
BINDIR = bin
PAPPL_DIR = external/pappl

.PHONY: all full pappl-init clean install uninstall help

# Default target: build the application (statically linked)
all:
	@$(MAKE) pappl-init
	@if [ ! -f "$(SRCDIR)/icon-48.h" ] || [ ! -f "$(SRCDIR)/icon-128.h" ] || [ ! -f "$(SRCDIR)/icon-512.h" ]; then \
		echo "Generating missing icon headers..."; \
		./scripts/generate-icon-headers.sh; \
	fi
	@echo "Generating version header..."
	@./scripts/generate-version.sh
	@mkdir -p $(BINDIR)
	@$(MAKE) -C $(SRCDIR) all

# Full build: clean and rebuild everything including PAPPL
full:
	@$(MAKE) clean
	@$(MAKE) pappl-init
	@echo "Patching PAPPL translations with custom strings..."
	@./scripts/patch-translations.sh
	@echo "Generating embedded icon headers..."
	@./scripts/generate-icon-headers.sh
	@echo "Generating version header..."
	@./scripts/generate-version.sh
	@echo "Rebuilding PAPPL from scratch, keeping config..."
	@$(MAKE) -C $(PAPPL_DIR) clean
	@if [ -n "$$ARCHFLAGS" ]; then \
		echo "Building PAPPL with ARCHFLAGS=$$ARCHFLAGS"; \
		$(MAKE) -C $(PAPPL_DIR) ARCHFLAGS="$$ARCHFLAGS"; \
	else \
		$(MAKE) -C $(PAPPL_DIR); \
	fi
	@mkdir -p $(BINDIR)
	@$(MAKE) -C $(SRCDIR) all

# Initialize PAPPL submodule if not already done
pappl-init:
ifndef package-build
	@if [ ! -f "$(PAPPL_DIR)/configure" ]; then \
		echo "Initializing PAPPL submodule..."; \
		git submodule update --init --recursive; \
	fi
endif
	@if [ ! -f "$(PAPPL_DIR)/pappl/libpappl.so" ] && [ ! -f "$(PAPPL_DIR)/pappl/libpappl.dylib" ] && [ ! -f "$(PAPPL_DIR)/pappl/libpappl.a" ]; then \
		echo "Patching PAPPL translations with custom strings..."; \
		./scripts/patch-translations.sh; \
		echo "Configuring and building PAPPL for the first time..."; \
		if [ -n "$$ARCHFLAGS" ]; then \
			echo "Building PAPPL with ARCHFLAGS=$$ARCHFLAGS"; \
			cd $(PAPPL_DIR) && ./configure && $(MAKE) ARCHFLAGS="$$ARCHFLAGS"; \
		elif [ "$$(uname -s)" = "Darwin" ] && [ "$$(uname -m)" = "x86_64" ]; then \
			echo "Detected macOS x86_64, configuring for single architecture..."; \
			cd $(PAPPL_DIR) && ./configure CFLAGS="-arch x86_64" LDFLAGS="-arch x86_64" && $(MAKE); \
		else \
			cd $(PAPPL_DIR) && ./configure && $(MAKE); \
		fi \
	fi

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@$(MAKE) -C $(SRCDIR) clean
	@rm -rf $(BINDIR)
	@rm -f $(SRCDIR)/icon-*.h
	@rm -f $(SRCDIR)/version.h
	@echo "Cleaning Debian build artifacts..."
	@rm -f ../*.deb ../*.buildinfo ../*.changes 2>/dev/null || true
	@rm -rf debian/.debhelper/ debian/tpcl-printer-app/ 2>/dev/null || true
	@rm -f debian/files debian/*.substvars debian/*.log debian/debhelper-build-stamp 2>/dev/null || true
ifndef package-build
	@echo "Resetting RPM spec file..."
	@git checkout -- tpcl-printer-app.spec 2>/dev/null || true
	@echo "Resetting Debian changelog..."
	@git checkout -- debian/changelog 2>/dev/null || true
	@echo "Resetting PAPPL submodule..."
	@git submodule update --init --force external/pappl
endif
	@echo "Clean complete."

# Install the application to system directories
install:
	@$(MAKE) -C $(SRCDIR) install

# Uninstall the application from system directories
uninstall:
	@$(MAKE) -C $(SRCDIR) uninstall

# Show help message
help:
	@echo "TPCL Printer Application - Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  make            - Build the application (default, static linking)"
	@echo "  make full       - Clean and rebuild everything including PAPPL"
	@echo "  make pappl-init - Initializes PAPPL submodule if needed"
	@echo "  make clean      - Remove build artifacts, reset RPM spec and PAPPL submodule"
	@echo "  make install    - Install to system directories (requires sudo)"
	@echo "  make uninstall  - Remove from system directories (requires sudo)"
	@echo "  make help       - Show this help message"
	@echo ""
	@echo "Build options:"
	@echo "  package-build=1 - Skip git submodule operations for package builds"
	@echo "  ARCHFLAGS=...   - Override architecture flags (e.g., ARCHFLAGS=\"-arch x86_64\")"
	@echo ""
	@echo "Build output:"
	@echo "  Binary will be placed in: $(BINDIR)/"
	@echo ""
	@echo "Platform notes:"
	@echo "  - macOS x86_64: Automatically configures for single architecture"
	@echo "  - macOS arm64:  Uses native architecture"
	@echo "  - Linux:        Uses native architecture"
	@echo ""
	@echo "Dependencies:"
	@echo "  - PAPPL (https://github.com/michaelrsweet/pappl.git)"
	@echo "  - CUPS development libraries"
	@echo "  - Additional libraries: openssl, jpeg, png, usb, pam"
	@echo "  - macOS: mDNSResponder (built-in)"
	@echo "  - Linux: Avahi client libraries"
	@echo ""
	@echo "Quick start:"
	@echo "  1. Run 'make' to build"
	@echo "  2. Run 'sudo $(BINDIR)/tpcl-printer-app server -o log-level=debug'"
	@echo ""

