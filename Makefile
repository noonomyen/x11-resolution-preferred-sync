BIN_NAME := x11-resolution-preferred-sync
SRC := x11-resolution-preferred-sync.cpp

BUILD_DIR := build

PREFIX := $(HOME)/.local
BIN_DIR := $(PREFIX)/bin

SYSTEMD_USER_DIR := $(HOME)/.config/systemd/user
SERVICE_SRC := x11-resolution-preferred-sync.service
SERVICE_DST := $(BUILD_DIR)/x11-resolution-preferred-sync.service

POLKIT_SRC := polkit-x11-resolution-preferred-sync.rules
POLKIT_DST := $(BUILD_DIR)/polkit-x11-resolution-preferred-sync.rules
POLKIT_RULE := /etc/polkit-1/rules.d/polkit-x11-resolution-preferred-sync.rules

USER_NAME := $(shell whoami)
DISPLAY_ENV ?= $(DISPLAY)
XAUTHORITY_ENV ?= $(XAUTHORITY)
ifeq ($(XAUTHORITY_ENV),)
    XAUTHORITY_ENV := $(HOME)/.Xauthority
endif

CXX := g++
CXXFLAGS := -O2 -Wall -Wextra -std=c++17

ifeq ($(DISABLE_SPICE_VDAGENTD_RESTART),1)
    CXXFLAGS += -DDISABLE_SPICE_VDAGENTD_RESTART
    $(info [INFO] Disable vdagent restart)
endif


.PHONY: build install uninstall clean

build:
	@echo "[BUILD] Creating build directory: $(BUILD_DIR)"
	mkdir -p "$(BUILD_DIR)"

	@echo "[BUILD] Compiling binary → $(BUILD_DIR)/$(BIN_NAME)"
	$(CXX) $(CXXFLAGS) "$(SRC)" -lX11 -lXrandr -o "$(BUILD_DIR)/$(BIN_NAME)"

	@echo "[BUILD] Generating service file with replacements"
	sed \
	    -e "s|@USER@|$(USER_NAME)|g" \
	    -e "s|@DISPLAY@|$(DISPLAY_ENV)|g" \
	    -e "s|@XAUTHORITY@|$(XAUTHORITY_ENV)|g" \
	    "$(SERVICE_SRC)" > "$(SERVICE_DST)"

	@echo "[BUILD] Generating polkit rule with replacements"
	sed \
	    -e "s|@USER@|$(USER_NAME)|g" \
	    "$(POLKIT_SRC)" > "$(POLKIT_DST)"

	@echo "[DONE] Build ready in $(BUILD_DIR)"


install:
	@if [ "$$(id -u)" = 0 ]; then \
		echo "[ERROR] Do not run install as root."; exit 1; \
	fi

	@echo "[INSTALL] Installing binary → $(BIN_DIR)"
	mkdir -p "$(BIN_DIR)"
	sudo cp "$(BUILD_DIR)/$(BIN_NAME)" "$(BIN_DIR)/"
	sudo chmod 755 "$(BIN_DIR)/$(BIN_NAME)"

	@echo "[INSTALL] Installing systemd user service → $(SYSTEMD_USER_DIR)"
	mkdir -p "$(SYSTEMD_USER_DIR)"
	cp "$(SERVICE_DST)" "$(SYSTEMD_USER_DIR)/"

	@echo "[INSTALL] Reloading systemd --user"
	systemctl --user daemon-reload
	systemctl --user enable x11-resolution-preferred-sync.service

	@echo "[INSTALL] Installing polkit rule → $(POLKIT_RULE)"
	sudo cp "$(POLKIT_DST)" "$(POLKIT_RULE)"
	sudo chmod 644 "$(POLKIT_RULE)"

	@echo ""
	@echo "[SUCCESS] Installation completed."
	@echo "Start with:"
	@echo "  systemctl --user start x11-resolution-preferred-sync.service"


uninstall:
	@if [ "$$(id -u)" = 0 ]; then \
		echo "[ERROR] Do not run uninstall as root."; exit 1; \
	fi

	@echo "[UNINSTALL] Stopping user service"
	-systemctl --user stop x11-resolution-preferred-sync.service

	@echo "[UNINSTALL] Disabling service"
	-systemctl --user disable x11-resolution-preferred-sync.service

	@echo "[UNINSTALL] Removing service file"
	rm -f "$(SYSTEMD_USER_DIR)/x11-resolution-preferred-sync.service"
	systemctl --user daemon-reload

	@echo "[UNINSTALL] Removing binary"
	sudo rm -f "$(BIN_DIR)/$(BIN_NAME)"

	@echo "[UNINSTALL] Removing polkit rule"
	sudo rm -f "$(POLKIT_RULE)"

	@echo "[SUCCESS] Uninstall completed."


clean:
	@echo "[CLEAN] Removing build directory"
	rm -rf "$(BUILD_DIR)"
	@echo "[DONE] Clean complete."
