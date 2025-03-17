#!/bin/bash

set -xe  # Exit on error and print each command

# Increase file descriptor limit
ulimit -n 4096

# Check prerequisites
for cmd in iw hostapd dnsmasq nmcli; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "$cmd required"; exit 1; }
done

# Variables
WLAN_IFACE="wlp0s20f3"    # Primary Wi-Fi interface
AP_IFACE="ap0"            # Virtual interface for hotspot
read -p "Enter SSID for Hotspot: " SSID
read -sp "Enter Password for hotspot(min 8 char): " PASS
echo ""

# Ensure NetworkManager is running
echo "Starting NetworkManager..."
sudo systemctl start NetworkManager
systemctl is-active NetworkManager || { echo "NetworkManager failed to start"; exit 1; }

# Verify primary wireless connection
echo "Checking $WLAN_IFACE connection..."
CONNECTION=$(nmcli -t -f NAME,DEVICE con show --active | grep "$WLAN_IFACE" | cut -d: -f1)
if [ -z "$CONNECTION" ]; then
    echo "Error: $WLAN_IFACE not connected."
    nmcli dev status
    exit 1
fi
echo "Connected via: $CONNECTION"

# Extract channel and frequency info from primary interface
WLAN_INFO=$(iw dev "$WLAN_IFACE" info)
CHANNEL=$(echo "$WLAN_INFO" | grep "channel" | awk '{print $2}')
FREQ=$(echo "$WLAN_INFO" | grep "channel" | awk -F'[()]' '{print $2}' | grep -o '[0-9]\+')
echo "Primary connection - Channel: $CHANNEL, Frequency: $FREQ MHz"
if [ -z "$CHANNEL" ] || [ -z "$FREQ" ]; then
  echo "Failed to extract channel or frequency information."
  exit 1
fi

# Check if primary connection is 5 GHz.
if [ "$FREQ" -lt 5000 ]; then
  echo "Error: Primary connection is on 2.4 GHz. Many wireless cards do not support simultaneous client and AP mode in 2.4 GHz."
  echo "Please connect to a 5 GHz network or use a separate Wi-Fi adapter for the hotspot."
  exit 1
fi

# Set hardware mode based on frequency (should be 5 GHz now)
HW_MODE="a"
echo "Using hardware mode: $HW_MODE"

# Clean up any existing AP interface
if sudo iw dev "$AP_IFACE" info >/dev/null 2>&1; then
    echo "Interface $AP_IFACE already exists. Removing it..."
    sudo iw dev "$AP_IFACE" del
fi

# Create the AP interface on the same card
echo "Creating $AP_IFACE..."
sudo iw dev "$WLAN_IFACE" interface add "$AP_IFACE" type __ap || { echo "Failed to create AP interface $AP_IFACE"; exit 1; }
sudo nmcli dev set "$AP_IFACE" managed no

# Verify internet connectivity and attempt reconnect if needed
echo "Checking internet connectivity..."
if ! ping -c 2 8.8.8.8 >/dev/null 2>&1; then
    echo "Internet appears down; attempting to reconnect..."
    sudo nmcli con up "$CONNECTION"
    sleep 2
    if ! ping -c 2 8.8.8.8 >/dev/null 2>&1; then
        echo "Failed to restore internet connection."
        sudo iw dev "$AP_IFACE" del
        exit 1
    fi
fi

# Configure hostapd using primary connection's channel
echo "Configuring hostapd..."
sudo bash -c "cat <<EOF > /tmp/hostapd.conf
interface=$AP_IFACE
driver=nl80211
ssid=$SSID
hw_mode=$HW_MODE
channel=$CHANNEL
wpa=2
wpa_passphrase=$PASS
wpa_key_mgmt=WPA-PSK
wpa_pairwise=CCMP
rsn_pairwise=CCMP
EOF"

# Stop any existing dnsmasq to prevent conflicts
if pgrep dnsmasq >/dev/null; then
    echo "Stopping existing dnsmasq..."
    sudo killall dnsmasq
fi

# Start hostapd and verify it starts correctly
echo "Starting hostapd..."
sudo hostapd /tmp/hostapd.conf &
HOSTAPD_PID=$!
sleep 2
if ! ps -p $HOSTAPD_PID >/dev/null; then
    echo "hostapd failed to start. Configuration:"
    cat /tmp/hostapd.conf
    sudo iw dev "$AP_IFACE" del
    exit 1
fi

# Configure IP and start dnsmasq for DHCP
echo "Setting up IP and DHCP for $AP_IFACE..."
sudo ip addr add 192.168.4.1/24 dev "$AP_IFACE"
sudo ip link set "$AP_IFACE" up
sudo dnsmasq --interface="$AP_IFACE" --dhcp-range=192.168.4.2,192.168.4.100,12h &

# Enable NAT for internet sharing
echo "Enabling NAT..."
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -o "$WLAN_IFACE" -j MASQUERADE
sudo iptables -A FORWARD -i "$AP_IFACE" -o "$WLAN_IFACE" -j ACCEPT
sudo iptables -A FORWARD -i "$WLAN_IFACE" -o "$AP_IFACE" -m state --state RELATED,ESTABLISHED -j ACCEPT

echo "Hotspot started on channel $CHANNEL using interface $AP_IFACE. Press Ctrl+C to stop."

# Cleanup on termination
trap 'echo "Stopping hotspot..."; sudo kill $HOSTAPD_PID; sudo killall dnsmasq; sudo iw dev "$AP_IFACE" del; exit 0' INT
wait
