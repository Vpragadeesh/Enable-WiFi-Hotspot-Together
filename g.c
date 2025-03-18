#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Define constants
#define WLAN_IFACE "wlp0s20f3" // Primary Wi-Fi interface
#define AP_IFACE "ap0"         // Virtual interface for hotspot
#define HOSTAPD_CONF "/tmp/hostapd.conf"

// Global variables for process IDs
pid_t hostapd_pid = -1;
pid_t dnsmasq_pid = -1;

// Function to execute shell commands and check for errors
int exec_command(const char *cmd) {
  int ret = system(cmd);
  if (ret != 0) {
    fprintf(stderr, "Command failed: %s\n", cmd);
  }
  return ret;
}

// Function to check if a command is available
int command_exists(const char *cmd) {
  char check_cmd[256];
  snprintf(check_cmd, sizeof(check_cmd), "command -v %s >/dev/null 2>&1", cmd);
  return system(check_cmd) == 0;
}

// Cleanup function to stop processes and remove AP interface
void cleanup() {
  printf("Stopping hotspot...\n");
  if (hostapd_pid > 0) {
    kill(hostapd_pid, SIGTERM);
  }
  if (dnsmasq_pid > 0) {
    kill(dnsmasq_pid, SIGTERM);
  }
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "sudo iw dev %s del 2>/dev/null", AP_IFACE);
  exec_command(cmd);
  exit(0);
}

// Signal handler for interrupts
void signal_handler(int signum) { cleanup(); }

int main() {
  // Set up signal handling for graceful termination
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // Increase file descriptor limit
  if (system("ulimit -n 4096") != 0) {
    perror("Failed to set ulimit");
    exit(1);
  }

  // Check prerequisites
  const char *prerequisites[] = {"iw", "hostapd", "dnsmasq", "nmcli"};
  for (int i = 0; i < sizeof(prerequisites) / sizeof(prerequisites[0]); i++) {
    if (!command_exists(prerequisites[i])) {
      fprintf(stderr, "%s required\n", prerequisites[i]);
      exit(1);
    }
  }

  // Prompt user for SSID and Password
  char ssid[256];
  char pass[256];
  printf("Enter SSID for hotspot: ");
  if (fgets(ssid, sizeof(ssid), stdin) == NULL) {
    perror("Failed to read SSID");
    exit(1);
  }
  ssid[strcspn(ssid, "\n")] = 0; // Remove newline

  printf("Enter Password for hotspot: ");
  if (fgets(pass, sizeof(pass), stdin) == NULL) {
    perror("Failed to read password");
    exit(1);
  }
  pass[strcspn(pass, "\n")] = 0; // Remove newline

  // Ensure NetworkManager is running
  printf("Starting NetworkManager...\n");
  if (exec_command("sudo systemctl start NetworkManager") != 0) {
    exit(1);
  }
  if (system("systemctl is-active NetworkManager") != 0) {
    fprintf(stderr, "NetworkManager failed to start\n");
    exit(1);
  }

  // Verify primary wireless connection
  printf("Checking %s connection...\n", WLAN_IFACE);
  char cmd[256];
  snprintf(cmd, sizeof(cmd),
           "nmcli -t -f NAME,DEVICE con show --active | grep %s | cut -d: -f1",
           WLAN_IFACE);
  FILE *fp = popen(cmd, "r");
  if (fp == NULL) {
    perror("Failed to execute nmcli");
    exit(1);
  }
  char connection[256];
  if (fgets(connection, sizeof(connection), fp) == NULL) {
    fprintf(stderr, "Error: %s not connected.\n", WLAN_IFACE);
    pclose(fp);
    exit(1);
  }
  pclose(fp);
  connection[strcspn(connection, "\n")] = 0;
  printf("Connected via: %s\n", connection);

  // Extract channel and frequency info from primary interface
  snprintf(cmd, sizeof(cmd), "iw dev %s info", WLAN_IFACE);
  fp = popen(cmd, "r");
  if (fp == NULL) {
    perror("Failed to execute iw");
    exit(1);
  }
  char line[256];
  int channel = -1;
  int freq = -1;
  while (fgets(line, sizeof(line), fp) != NULL) {
    if (strstr(line, "channel") != NULL) {
      sscanf(line, "\tchannel %d (%d MHz)", &channel, &freq);
      break;
    }
  }
  pclose(fp);
  if (channel == -1 || freq == -1) {
    fprintf(stderr, "Failed to extract channel or frequency information.\n");
    exit(1);
  }
  printf("Primary connection - Channel: %d, Frequency: %d MHz\n", channel,
         freq);

  // Check if primary connection is 5 GHz
  if (freq < 5000) {
    fprintf(stderr,
            "Error: Primary connection is on 2.4 GHz. Many wireless cards do "
            "not support simultaneous client and AP mode in 2.4 GHz.\n");
    fprintf(stderr, "Please connect to a 5 GHz network or use a separate Wi-Fi "
                    "adapter for the hotspot.\n");
    exit(1);
  }

  // Set hardware mode based on frequency (5 GHz)
  const char *hw_mode = "a";
  printf("Using hardware mode: %s\n", hw_mode);

  // Clean up any existing AP interface
  snprintf(cmd, sizeof(cmd), "sudo iw dev %s info >/dev/null 2>&1", AP_IFACE);
  if (system(cmd) == 0) {
    printf("Interface %s already exists. Removing it...\n", AP_IFACE);
    snprintf(cmd, sizeof(cmd), "sudo iw dev %s del", AP_IFACE);
    if (exec_command(cmd) != 0) {
      exit(1);
    }
  }

  // Create the AP interface
  printf("Creating %s...\n", AP_IFACE);
  snprintf(cmd, sizeof(cmd), "sudo iw dev %s interface add %s type __ap",
           WLAN_IFACE, AP_IFACE);
  if (exec_command(cmd) != 0) {
    fprintf(stderr, "Failed to create AP interface %s\n", AP_IFACE);
    exit(1);
  }
  snprintf(cmd, sizeof(cmd), "sudo nmcli dev set %s managed no", AP_IFACE);
  if (exec_command(cmd) != 0) {
    exit(1);
  }

  // Verify internet connectivity and attempt reconnect if needed
  printf("Checking internet connectivity...\n");
  if (system("ping -c 2 8.8.8.8 >/dev/null 2>&1") != 0) {
    printf("Internet appears down; attempting to reconnect...\n");
    snprintf(cmd, sizeof(cmd), "sudo nmcli con up %s", connection);
    if (exec_command(cmd) != 0) {
      exit(1);
    }
    sleep(2);
    if (system("ping -c 2 8.8.8.8 >/dev/null 2>&1") != 0) {
      fprintf(stderr, "Failed to restore internet connection.\n");
      snprintf(cmd, sizeof(cmd), "sudo iw dev %s del", AP_IFACE);
      exec_command(cmd);
      exit(1);
    }
  }

  // Configure hostapd
  printf("Configuring hostapd...\n");
  FILE *conf_fp = fopen(HOSTAPD_CONF, "w");
  if (conf_fp == NULL) {
    perror("Failed to create hostapd.conf");
    exit(1);
  }
  fprintf(conf_fp, "interface=%s\n", AP_IFACE);
  fprintf(conf_fp, "driver=nl80211\n");
  fprintf(conf_fp, "ssid=%s\n", ssid);
  fprintf(conf_fp, "hw_mode=%s\n", hw_mode);
  fprintf(conf_fp, "channel=%d\n", channel);
  fprintf(conf_fp, "wpa=2\n");
  fprintf(conf_fp, "wpa_passphrase=%s\n", pass);
  fprintf(conf_fp, "wpa_key_mgmt=WPA-PSK\n");
  fprintf(conf_fp, "wpa_pairwise=CCMP\n");
  fprintf(conf_fp, "rsn_pairwise=CCMP\n");
  fclose(conf_fp);

  // Stop any existing dnsmasq
  if (system("pgrep dnsmasq >/dev/null") == 0) {
    printf("Stopping existing dnsmasq...\n");
    exec_command("sudo killall dnsmasq");
  }

  // Start hostapd
  printf("Starting hostapd...\n");
  hostapd_pid = fork();
  if (hostapd_pid == 0) {
    execlp("sudo", "sudo", "hostapd", HOSTAPD_CONF, NULL);
    perror("Failed to start hostapd");
    exit(1);
  } else if (hostapd_pid > 0) {
    sleep(2);
    if (waitpid(hostapd_pid, NULL, WNOHANG) != 0) {
      fprintf(stderr, "hostapd failed to start. Configuration:\n");
      system("cat " HOSTAPD_CONF);
      snprintf(cmd, sizeof(cmd), "sudo iw dev %s del", AP_IFACE);
      exec_command(cmd);
      exit(1);
    }
  } else {
    perror("Failed to fork for hostapd");
    exit(1);
  }

  // Configure IP address and start dnsmasq
  printf("Setting up IP and DHCP for %s...\n", AP_IFACE);
  snprintf(cmd, sizeof(cmd), "sudo ip addr add 192.168.4.1/24 dev %s",
           AP_IFACE);
  if (exec_command(cmd) != 0) {
    cleanup();
  }
  snprintf(cmd, sizeof(cmd), "sudo ip link set %s up", AP_IFACE);
  if (exec_command(cmd) != 0) {
    cleanup();
  }
  dnsmasq_pid = fork();
  if (dnsmasq_pid == 0) {
    execlp("sudo", "sudo", "dnsmasq", "--interface", AP_IFACE,
           "--dhcp-range=192.168.4.2,192.168.4.100,12h", NULL);
    perror("Failed to start dnsmasq");
    exit(1);
  } else if (dnsmasq_pid < 0) {
    perror("Failed to fork for dnsmasq");
    cleanup();
  }

  // Enable NAT for internet sharing
  printf("Enabling NAT...\n");
  if (exec_command("sudo sysctl -w net.ipv4.ip_forward=1") != 0) {
    cleanup();
  }
  snprintf(cmd, sizeof(cmd),
           "sudo iptables -t nat -A POSTROUTING -o %s -j MASQUERADE",
           WLAN_IFACE);
  if (exec_command(cmd) != 0) {
    cleanup();
  }
  snprintf(cmd, sizeof(cmd), "sudo iptables -A FORWARD -i %s -o %s -j ACCEPT",
           AP_IFACE, WLAN_IFACE);
  if (exec_command(cmd) != 0) {
    cleanup();
  }
  snprintf(cmd, sizeof(cmd),
           "sudo iptables -A FORWARD -i %s -o %s -m state --state "
           "RELATED,ESTABLISHED -j ACCEPT",
           WLAN_IFACE, AP_IFACE);
  if (exec_command(cmd) != 0) {
    cleanup();
  }

  printf("Hotspot started on channel %d using interface %s. Press Ctrl+C to "
         "stop.\n",
         channel, AP_IFACE);

  // Wait indefinitely
  while (1) {
    sleep(1);
  }

  return 0;
}
