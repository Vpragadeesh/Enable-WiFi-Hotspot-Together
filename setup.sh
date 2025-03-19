#!/bin/bash
# setup.sh – Script to install dependencies and compile the hotspot program

set -e

# Function to install packages using the available package manager.
install_packages() {
    packages="$@"
    if command -v apt-get >/dev/null 2>&1; then
        echo "Using apt-get to install: $packages"
        sudo apt-get update
        sudo apt-get install -y $packages
    elif command -v dnf >/dev/null 2>&1; then
        echo "Using dnf to install: $packages"
        sudo dnf install -y $packages
    elif command -v pacman >/dev/null 2>&1; then
        echo "Using pacman to install: $packages"
        sudo pacman -Sy --noconfirm $packages
    else
        echo "No supported package manager found (apt-get, dnf, pacman). Please install the following packages manually: $packages"
        exit 1
    fi
}

# List of required commands and their corresponding package names.
declare -A REQUIRED_CMDS
REQUIRED_CMDS=(
  [iw]="iw"
  [hostapd]="hostapd"
  [dnsmasq]="dnsmasq"
  [nmcli]="network-manager"
  [systemctl]="systemd"   # systemctl is typically part of systemd
  [ip]="iproute2"
  [iptables]="iptables"
  [gcc]="gcc"
)

# Determine which packages are missing.
MISSING_PACKAGES=()
for cmd in "${!REQUIRED_CMDS[@]}"; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Command '$cmd' not found. It is provided by package '${REQUIRED_CMDS[$cmd]}'."
        MISSING_PACKAGES+=("${REQUIRED_CMDS[$cmd]}")
    fi
done

if [ ${#MISSING_PACKAGES[@]} -gt 0 ]; then
    echo "Installing missing packages: ${MISSING_PACKAGES[*]}"
    install_packages "${MISSING_PACKAGES[@]}"
fi

# Compile the C program.
if [ ! -f my.c ]; then
    echo "Source file my.c not found in the current directory."
    exit 1
fi

echo "Compiling my.c..."
gcc -o myc my.c

echo "Compilation successful."
echo "You can now run the program (it requires root privileges):"
echo "  sudo ./myc"
