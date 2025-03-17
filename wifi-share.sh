#!/bin/bash

set -x  # Print each command as it runs

# Check prerequisites
command -v iw >/dev/null 2>&1 || { echo "iw required"; exit 1; }
command -v hostapd >/dev/null 2>&1 || { echo "hostapd required"; exit 1; }
command -v dnsmasq >/dev/null 2>&1 || { echo "dnsmasq required"; exit 1; }
command -v nmcli >/dev/null 2>&1 || { echo "nmcli required"; exit 1; }

# Variables
WLAN_IFACE="wlp0s20f3"
AP_IFACE="ap0"
SSID="MyHotspot"
PASS="MyPassword"

# Ensure NetworkManager
echo "Checking NetworkManager..."
sudo systemctl start NetworkManager
systemctl is-active NetworkManager || { echo "NetworkManager failed to start"; exit 1; }

# Verify connection
echo "Checking $WLAN_IFACE connection..."
CHANNEL=$(iw dev "$WLAN_IFACE" info | grep channel | awk '{print $2}')
CONNECTION=$(nmcli -t -f NAME,DEVICE con show --active | grep "$WLAN_IFACE" | cut -d: -f1)
echo "Channel: $CHANNEL"
echo "Connection: $CONNECTION"
if [ -z "$CHANNEL" ] || [ -z "$CONNECTION" ]; then
    echo "Error: $WLAN_IFACE not connected."
    nmcli dev status
    exit 1
fi

# Create ap0
echo "Creating $AP_IFACE..."
sudo iw dev "$WLAN_IFACE" interface add "$AP_IFACE" type __ap || { echo "Failed to create AP"; exit 1; }
sudo nmcli dev set "$AP_IFACE" managed no

# Reconnect if dropped
echo "Checking internet..."
if ! ping -c 2 8.8.8.8 >/dev/null 2>&1; then
    echo "Wi-Fi dropped, reconnecting..."
    sudo nmcli con up "$CONNECTION"
    if ! ping -c 2 8.8.8.8 >/dev/null 2>&1; then
        echo "Failed to restore Wi-Fi."
        sudo iw dev "$AP_IFACE" del
        exit 1
    fi
fi

# Configure hostapd
echo "Configuring hostapd..."
sudo bash -c "cat <<EOF > /tmp/hostapd.conf
interface=$AP_IFACE
driver=nl80211
ssid=$SSID
hw_mode=a
channel=$CHANNEL
wpa=2
wpa_passphrase=$PASS
wpa_key_mgmt=WPA-PSK
wpa_pairwise=CCMP
rsn_pairwise=CCMP
EOF"

# Check for existing dnsmasq
if pgrep dnsmasq >/dev/null; then
    echo "Stopping existing dnsmasq..."
    sudo killall dnsmasq
fi

# Start hostapd
echo "Starting hostapd..."
sudo hostapd /tmp/hostapd.conf & HOSTAPD_PID=$!
if ! ps -p $HOSTAPD_PID >/dev/null; then
    echo "hostapd failed to start."
    cat /tmp/hostapd.conf
    sudo iw dev "$AP_IFACE" del
    exit 1
fi

# Configure IP and DHCP
echo "Setting up $AP_IFACE..."
sudo ip addr add 192.168.4.1/24 dev "$AP_IFACE"
sudo ip link set "$AP_IFACE" up
sudo dnsmasq --interface="$AP_IFACE" --dhcp-range=192.168.4.2,192.168.4.100,12h &

# Enable NAT
echo "Enabling NAT..."
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -o "$WLAN_IFACE" -j MASQUERADE
sudo iptables -A FORWARD -i "$AP_IFACE" -o "$WLAN_IFACE" -j ACCEPT
sudo iptables -A FORWARD -i "$WLAN_IFACE" -o "$AP_IFACE" -m state --state RELATED,ESTABLISHED -j ACCEPT

echo "Hotspot started on channel $CHANNEL. Press Ctrl+C to stop."
trap 'sudo kill $HOSTAPD_PID; sudo killall dnsmasq; sudo iw dev "$AP_IFACE" del; echo "Hotspot stopped."; exit' INT
wait
