#!/bin/bash
# setup.sh – Setup script to install dependencies, stop conflicts, clean up interfaces,
# disable Wi-Fi power saving, and compile the hotspot program from source file "my.c"
# to output binary "myc".

set -e

# Function to install packages using available package manager.
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
        echo "No supported package manager found. Please install the following packages manually: $packages"
        exit 1
    fi
}

# List of required commands and corresponding package names.
declare -A REQUIRED_CMDS
REQUIRED_CMDS=(
  [iw]="iw"
  [hostapd]="hostapd"
  [dnsmasq]="dnsmasq"
  [nmcli]="network-manager"
  [systemctl]="systemd"   # Typically part of systemd
  [ip]="iproute2"
  [iptables]="iptables"
  [gcc]="gcc"
)

# Check for missing commands.
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

# Stop systemd-resolved to avoid conflict with dnsmasq on port 53.
if systemctl is-active --quiet systemd-resolved; then
    echo "Stopping and disabling systemd-resolved (it may conflict with dnsmasq)..."
    sudo systemctl stop systemd-resolved
    sudo systemctl disable systemd-resolved
fi

# Ensure port 53 is free (both TCP and UDP).
echo "Freeing port 53..."
sudo fuser -k 53/tcp 2>/dev/null || true
sudo fuser -k 53/udp 2>/dev/null || true

# Disable Wi-Fi power saving to avoid disconnections.
echo "Disabling Wi-Fi power saving..."
CONF_FILE="/etc/NetworkManager/conf.d/default-wifi-powersave-on.conf"
sudo mkdir -p "$(dirname $CONF_FILE)"
sudo tee "$CONF_FILE" >/dev/null <<EOF
[connection]
wifi.powersave = 2
EOF
# Restart NetworkManager to apply changes.
sudo systemctl restart NetworkManager

# Clean up any leftover AP interface (ap0).
if ip link show ap0 >/dev/null 2>&1; then
    echo "Interface ap0 already exists. Deleting it..."
    sudo ip link delete ap0
fi

# Optionally verify that the wireless device supports AP mode.
echo "Verifying wireless device supports AP mode..."
if ! iw list | grep -q "AP"; then
    echo "Warning: Your wireless device may not support AP mode."
    echo "Please check 'iw list' to confirm supported interface modes."
fi

# Compile the C program from my.c to output binary myc.
if [ ! -f my.c ]; then
    echo "Source file my.c not found in the current directory."
    exit 1
fi

echo "Compiling my.c..."
gcc -o myc my.c
echo "Compilation successful."

cat <<EOF

Setup complete.
You can now run the hotspot program (it requires root privileges):
  sudo ./myc

This program will:
 - Start NetworkManager and create a hotspot with the provided SSID and password.
 - Continuously monitor internet connectivity using ping.
 - Automatically switch to the best available saved Wi-Fi network if connectivity is lost.

EOF
