#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#define AP_IFACE "ap0"
#define HOSTAPD_CONF "/tmp/hostapd.conf"
#define AP_IP "192.168.4.1/24"
#define DHCP_RANGE "192.168.4.2,192.168.4.100,12h"
#define CONFIG_FILE "/tmp/hotspot.conf" // file to persist SSID and password

pid_t hostapd_pid = -1;

// Helper function to run a command and capture its output.
char *exec_cmd(const char *cmd) {
  FILE *fp;
  char *result = NULL;
  size_t size = 0;
  char buffer[256];

  fp = popen(cmd, "r");
  if (fp == NULL) {
    perror("popen failed");
    return NULL;
  }
  while (fgets(buffer, sizeof(buffer), fp) != NULL) {
    size_t len = strlen(buffer);
    result = realloc(result, size + len + 1);
    if (!result) {
      perror("realloc");
      break;
    }
    memcpy(result + size, buffer, len);
    size += len;
    result[size] = '\0';
  }
  pclose(fp);
  return result;
}

// Helper function to get full path of a command.
char *get_cmd_path(const char *cmd) {
  char buf[256];
  snprintf(buf, sizeof(buf), "command -v %s", cmd);
  char *path = exec_cmd(buf);
  if (path) {
    path[strcspn(path, "\n")] = '\0'; // Remove newline
  }
  return path;
}

// Check that dnsmasq is running.
int check_dnsmasq_running(const char *dnsmasq_path) {
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "pgrep -x %s >/dev/null 2>&1", "dnsmasq");
  return (system(cmd) == 0);
}

// Check that the AP interface has the expected IP.
int check_ap_ip(const char *ip_path) {
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "%s addr show %s", ip_path, AP_IFACE);
  char *output = exec_cmd(cmd);
  if (!output)
    return 0;
  int ok = (strstr(output, "192.168.4.1") != NULL);
  free(output);
  return ok;
}

// Retrieve saved Wi-Fi connection names (assumed to match SSIDs)
char **get_saved_connections(const char *nmcli_path, int *count) {
  char *saved = exec_cmd("nmcli -t -f NAME connection show");
  if (!saved) {
    *count = 0;
    return NULL;
  }
  int lines = 0;
  for (char *p = saved; *p; p++) {
    if (*p == '\n')
      lines++;
  }
  char **list = malloc(sizeof(char *) * lines);
  if (!list) {
    free(saved);
    *count = 0;
    return NULL;
  }
  int idx = 0;
  char *line = strtok(saved, "\n");
  while (line && idx < lines) {
    list[idx] = strdup(line);
    idx++;
    line = strtok(NULL, "\n");
  }
  *count = idx;
  free(saved);
  return list;
}

// Retrieve available Wi-Fi networks: returns an array of structures (SSID and
// signal)
typedef struct {
  char ssid[128];
  int signal;
} WifiEntry;

WifiEntry *get_available_networks(const char *nmcli_path, int *count) {
  char *output = exec_cmd("nmcli -t -f SSID,SIGNAL device wifi list");
  if (!output) {
    *count = 0;
    return NULL;
  }
  if (strlen(output) == 0) {
    fprintf(
        stderr,
        "Warning: 'nmcli device wifi list' returned empty. "
        "Your device/driver may not support scanning in AP mode on this OS.\n");
    *count = 0;
    free(output);
    return NULL;
  }
  int lines = 0;
  for (char *p = output; *p; p++) {
    if (*p == '\n')
      lines++;
  }
  WifiEntry *networks = malloc(sizeof(WifiEntry) * lines);
  if (!networks) {
    free(output);
    *count = 0;
    return NULL;
  }
  int idx = 0;
  char *line = strtok(output, "\n");
  while (line && idx < lines) {
    char *colon = strchr(line, ':');
    if (colon) {
      *colon = '\0';
      strncpy(networks[idx].ssid, line, sizeof(networks[idx].ssid) - 1);
      networks[idx].ssid[sizeof(networks[idx].ssid) - 1] = '\0';
      networks[idx].signal = atoi(colon + 1);
      idx++;
    }
    line = strtok(NULL, "\n");
  }
  *count = idx;
  free(output);
  return networks;
}

// Automatically switch to the best saved Wi-Fi (highest signal)
// Returns 0 on success, nonzero on failure.
int auto_switch_wifi(const char *nmcli_path) {
  int savedCount = 0, availCount = 0;
  char **saved = get_saved_connections(nmcli_path, &savedCount);
  if (savedCount == 0) {
    fprintf(stderr, "No saved Wi-Fi connections found.\n");
    return 1;
  }
  WifiEntry *avail = get_available_networks(nmcli_path, &availCount);
  if (availCount == 0) {
    fprintf(stderr, "No available Wi-Fi networks detected. Auto-switching is "
                    "not supported on this system.\n");
    for (int i = 0; i < savedCount; i++)
      free(saved[i]);
    free(saved);
    return 1;
  }
  int bestSignal = -1;
  char bestSSID[128] = "";
  for (int i = 0; i < availCount; i++) {
    if (strlen(avail[i].ssid) == 0)
      continue;
    for (int j = 0; j < savedCount; j++) {
      if (strcmp(avail[i].ssid, saved[j]) == 0) {
        if (avail[i].signal > bestSignal) {
          bestSignal = avail[i].signal;
          strncpy(bestSSID, avail[i].ssid, sizeof(bestSSID) - 1);
          bestSSID[sizeof(bestSSID) - 1] = '\0';
        }
        break;
      }
    }
  }
  for (int i = 0; i < savedCount; i++)
    free(saved[i]);
  free(saved);
  free(avail);

  if (bestSignal == -1) {
    fprintf(stderr, "No known Wi-Fi networks are currently in range.\n");
    return 1;
  }
  printf("Best candidate found: \"%s\" with signal strength %d\n", bestSSID,
         bestSignal);
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "sudo %s con up \"%s\"", nmcli_path, bestSSID);
  printf("Attempting to connect to \"%s\"...\n", bestSSID);
  if (system(cmd) != 0) {
    fprintf(stderr, "Failed to activate connection for \"%s\".\n", bestSSID);
    return 1;
  }
  sleep(2);
  if (system("ping -c 2 google.com >/dev/null 2>&1") != 0) {
    fprintf(
        stderr,
        "Connection attempt to \"%s\" did not restore internet connectivity.\n",
        bestSSID);
    return 1;
  } else {
    printf("Reconnected to \"%s\" successfully!\n", bestSSID);
  }
  return 0;
}

// Check if systemd-resolved is active and warn the user.
void check_systemd_resolved() {
  if (system("systemctl is-active --quiet systemd-resolved") == 0) {
    fprintf(stderr, "Warning: systemd-resolved is active. It may conflict with "
                    "dnsmasq on port 53.\n");
  }
}

// Function to load hotspot configuration (SSID and password) from file,
// or prompt the user if not present.
void load_hotspot_config(char *ssid, size_t ssid_len, char *pass,
                         size_t pass_len) {
  FILE *config = fopen(CONFIG_FILE, "r");
  if (config) {
    if (fgets(ssid, ssid_len, config) != NULL) {
      ssid[strcspn(ssid, "\n")] = '\0';
    }
    if (fgets(pass, pass_len, config) != NULL) {
      pass[strcspn(pass, "\n")] = '\0';
    }
    fclose(config);
    printf("Using saved hotspot configuration:\n  SSID: %s\n", ssid);
  } else {
    printf("Enter SSID for hotspot: ");
    if (!fgets(ssid, ssid_len, stdin)) {
      fprintf(stderr, "Error reading SSID\n");
      exit(1);
    }
    ssid[strcspn(ssid, "\n")] = '\0';

    printf("Enter Password for hotspot: ");
    if (!fgets(pass, pass_len, stdin)) {
      fprintf(stderr, "Error reading Password\n");
      exit(1);
    }
    pass[strcspn(pass, "\n")] = '\0';
    printf("\n");
    config = fopen(CONFIG_FILE, "w");
    if (!config) {
      perror("fopen config file for writing");
      exit(1);
    }
    fprintf(config, "%s\n%s\n", ssid, pass);
    fclose(config);
  }
}

// Cleanup function to be called on SIGINT/SIGTERM.
void cleanup_handler(int sig) {
  printf("\nStopping hotspot...\n");
  if (hostapd_pid > 0) {
    kill(hostapd_pid, SIGTERM);
  }
  system("sudo killall dnsmasq 2>/dev/null");
  char delCmd[128];
  snprintf(delCmd, sizeof(delCmd), "sudo iw dev %s del", AP_IFACE);
  system(delCmd);
  exit(0);
}

int main(void) {
  signal(SIGINT, cleanup_handler);
  signal(SIGTERM, cleanup_handler);

  // Increase file descriptor limit.
  struct rlimit rl;
  if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
    rl.rlim_cur = 4096;
    setrlimit(RLIMIT_NOFILE, &rl);
  }

  // Fetch full paths for required commands.
  char *iw_path = get_cmd_path("iw");
  char *hostapd_path = get_cmd_path("hostapd");
  char *dnsmasq_path = get_cmd_path("dnsmasq");
  char *nmcli_path = get_cmd_path("nmcli");
  char *systemctl_path = get_cmd_path("systemctl");
  char *ip_path = get_cmd_path("ip");
  char *iptables_path = get_cmd_path("iptables");

  if (!iw_path || !hostapd_path || !dnsmasq_path || !nmcli_path ||
      !systemctl_path || !ip_path || !iptables_path) {
    fprintf(stderr, "One or more required tools are missing.\n");
    exit(1);
  }

  printf("Found tools:\n");
  printf("iw:         %s\n", iw_path);
  printf("hostapd:    %s\n", hostapd_path);
  printf("dnsmasq:    %s\n", dnsmasq_path);
  printf("nmcli:      %s\n", nmcli_path);
  printf("systemctl:  %s\n", systemctl_path);
  printf("ip:         %s\n", ip_path);
  printf("iptables:   %s\n", iptables_path);

  check_systemd_resolved();

  // Fetch the connected WLAN interface using nmcli.
  char *wlan_iface = exec_cmd("nmcli -t -f DEVICE,TYPE,STATE dev status | grep "
                              "':wifi:connected' | cut -d: -f1 | head -n1");
  if (!wlan_iface || strlen(wlan_iface) == 0) {
    fprintf(stderr, "No connected WLAN interface detected.\n");
    exit(1);
  }
  wlan_iface[strcspn(wlan_iface, "\n")] = '\0';
  printf("Detected connected WLAN interface: %s\n", wlan_iface);

  // Load hotspot configuration (SSID and password) from file or prompt.
  char ssid[128], pass[128];
  load_hotspot_config(ssid, sizeof(ssid), pass, sizeof(pass));

  // Prompt for connectivity check interval.
  int check_interval = 10; // default seconds
  char interval_input[16];
  printf("Enter connectivity check interval in seconds [default 10]: ");
  if (fgets(interval_input, sizeof(interval_input), stdin) != NULL) {
    if (interval_input[0] != '\n') {
      check_interval = atoi(interval_input);
      if (check_interval <= 0)
        check_interval = 10;
    }
  }
  printf("Using connectivity check interval: %d seconds\n", check_interval);

  // Start NetworkManager.
  char nmcli_start[256];
  snprintf(nmcli_start, sizeof(nmcli_start), "sudo %s start NetworkManager",
           systemctl_path);
  printf("Starting NetworkManager...\n");
  system(nmcli_start);
  if (system("systemctl is-active NetworkManager >/dev/null 2>&1") != 0) {
    fprintf(stderr, "NetworkManager failed to start\n");
    exit(1);
  }

  // Verify primary wireless connection via nmcli.
  char nmcliCmd[256];
  snprintf(nmcliCmd, sizeof(nmcliCmd),
           "%s -t -f NAME,DEVICE con show --active | grep \"%s\" | cut -d: -f1",
           nmcli_path, wlan_iface);
  char *connection = exec_cmd(nmcliCmd);
  if (!connection || strlen(connection) == 0) {
    fprintf(stderr, "Error: %s not connected.\n", wlan_iface);
    system("nmcli dev status");
    exit(1);
  }
  printf("Connected via: %s", connection);

  // Extract channel and frequency info using iw.
  char iwCmd[256];
  snprintf(iwCmd, sizeof(iwCmd), "%s dev %s info", iw_path, wlan_iface);
  char *wlanInfo = exec_cmd(iwCmd);
  if (!wlanInfo) {
    fprintf(stderr, "Failed to get wireless info\n");
    exit(1);
  }
  char channel[16] = {0};
  char freq[16] = {0};
  {
    char *line = strtok(wlanInfo, "\n");
    while (line) {
      if (strstr(line, "channel")) {
        sscanf(line, " channel %15s", channel);
        char *paren = strchr(line, '(');
        if (paren) {
          sscanf(paren, "(%15s", freq);
          char *m = strstr(freq, "MHz");
          if (m)
            *m = '\0';
        }
        break;
      }
      line = strtok(NULL, "\n");
    }
  }
  free(wlanInfo);
  if (strlen(channel) == 0 || strlen(freq) == 0) {
    fprintf(stderr, "Failed to extract channel or frequency information.\n");
    exit(1);
  }
  printf("Primary connection - Channel: %s, Frequency: %s MHz\n", channel,
         freq);

  const char *hw_mode = "a";
  printf("Using hardware mode: %s\n", hw_mode);

  // Remove any existing AP interface.
  char checkAP[128];
  snprintf(checkAP, sizeof(checkAP), "sudo %s dev %s info >/dev/null 2>&1",
           iw_path, AP_IFACE);
  if (system(checkAP) == 0) {
    printf("Interface %s already exists. Removing it...\n", AP_IFACE);
    char delCmd[128];
    snprintf(delCmd, sizeof(delCmd), "sudo %s dev %s del", iw_path, AP_IFACE);
    system(delCmd);
  }

  // Create the AP interface.
  char addIf[256];
  snprintf(addIf, sizeof(addIf), "sudo %s dev %s interface add %s type __ap",
           iw_path, wlan_iface, AP_IFACE);
  printf("Creating %s...\n", AP_IFACE);
  if (system(addIf) != 0) {
    fprintf(stderr, "Failed to create AP interface %s\n", AP_IFACE);
    exit(1);
  }
  char nmcliSet[128];
  snprintf(nmcliSet, sizeof(nmcliSet), "sudo %s dev set %s managed no",
           nmcli_path, AP_IFACE);
  system(nmcliSet);

  // Initial internet connectivity check.
  printf("Checking internet connectivity...\n");
  if (system("ping -c 2 google.com >/dev/null 2>&1") != 0) {
    if (auto_switch_wifi(nmcli_path) != 0) {
      fprintf(stderr, "Initial reconnection failed.\n");
      exit(1);
    }
  }
  free(connection);

  // Write hostapd configuration.
  printf("Configuring hostapd...\n");
  FILE *fp = fopen(HOSTAPD_CONF, "w");
  if (!fp) {
    perror("fopen hostapd config");
    exit(1);
  }
  fprintf(fp,
          "interface=%s\n"
          "driver=nl80211\n"
          "ssid=%s\n"
          "hw_mode=%s\n"
          "channel=%s\n"
          "wpa=2\n"
          "wpa_passphrase=%s\n"
          "wpa_key_mgmt=WPA-PSK\n"
          "wpa_pairwise=CCMP\n"
          "rsn_pairwise=CCMP\n",
          AP_IFACE, ssid, hw_mode, channel, pass);
  fclose(fp);

  // Stop any existing dnsmasq.
  if (system("pgrep dnsmasq >/dev/null 2>&1") == 0) {
    printf("Stopping existing dnsmasq...\n");
    system("sudo killall dnsmasq");
  }

  // Start hostapd.
  printf("Starting hostapd...\n");
  hostapd_pid = fork();
  if (hostapd_pid == 0) {
    execlp("sudo", "sudo", hostapd_path, HOSTAPD_CONF, NULL);
    perror("execlp hostapd failed");
    exit(1);
  }
  if (kill(hostapd_pid, 0) != 0) {
    fprintf(stderr, "hostapd failed to start. Configuration:\n");
    system("cat " HOSTAPD_CONF);
    char delCmd3[128];
    snprintf(delCmd3, sizeof(delCmd3), "sudo %s dev %s del", iw_path, AP_IFACE);
    system(delCmd3);
    exit(1);
  }

  // Set up IP and bring up the AP interface.
  char ipCmd[128];
  snprintf(ipCmd, sizeof(ipCmd), "sudo %s addr add %s dev %s", ip_path, AP_IP,
           AP_IFACE);
  system(ipCmd);
  char linkCmd[128];
  snprintf(linkCmd, sizeof(linkCmd), "sudo %s link set %s up", ip_path,
           AP_IFACE);
  system(linkCmd);

  if (!check_ap_ip(ip_path)) {
    fprintf(stderr, "AP interface %s did not receive the correct IP address.\n",
            AP_IFACE);
    exit(1);
  }

  // Start dnsmasq for DHCP, binding only to the hotspot's IP.
  char dnsCmd[256];
  snprintf(dnsCmd, sizeof(dnsCmd),
           "sudo %s --interface=%s --bind-interfaces "
           "--listen-address=192.168.4.1 --dhcp-range=%s &",
           dnsmasq_path, AP_IFACE, DHCP_RANGE);
  system(dnsCmd);

  int retry = 3;
  while (retry-- > 0) {
    sleep(2);
    if (check_dnsmasq_running(dnsmasq_path)) {
      printf("dnsmasq is running and DHCP is enabled.\n");
      break;
    }
    printf("Waiting for dnsmasq to start...\n");
  }
  if (retry < 0) {
    fprintf(stderr, "dnsmasq is not running. DHCP will not work.\n");
    exit(1);
  }

  // Enable NAT for internet sharing.
  printf("Enabling NAT...\n");
  system("sudo sysctl -w net.ipv4.ip_forward=1");
  char iptCmd[256];
  snprintf(iptCmd, sizeof(iptCmd),
           "sudo %s -t nat -A POSTROUTING -o %s -j MASQUERADE", iptables_path,
           wlan_iface);
  system(iptCmd);
  snprintf(iptCmd, sizeof(iptCmd), "sudo %s -A FORWARD -i %s -o %s -j ACCEPT",
           iptables_path, AP_IFACE, wlan_iface);
  system(iptCmd);
  snprintf(iptCmd, sizeof(iptCmd),
           "sudo %s -A FORWARD -i %s -o %s -m state --state "
           "RELATED,ESTABLISHED -j ACCEPT",
           iptables_path, wlan_iface, AP_IFACE);
  system(iptCmd);

  printf("Hotspot started on channel %s using interface %s.\n", channel,
         AP_IFACE);
  printf("Clients should obtain an IP address from dnsmasq.\n");
  printf("Press Ctrl+C to stop.\n");

  while (1) {
    sleep(check_interval);
    if (system("ping -c 2 google.com >/dev/null 2>&1") != 0) {
      printf("Internet connectivity lost. Attempting automatic switch...\n");
      if (auto_switch_wifi(nmcli_path) != 0) {
        fprintf(stderr, "Automatic switching failed. Retrying...\n");
      }
    } else {
      printf("Internet connection stable.\n");
    }
  }

  free(iw_path);
  free(hostapd_path);
  free(dnsmasq_path);
  free(nmcli_path);
  free(systemctl_path);
  free(ip_path);
  free(iptables_path);
  free(wlan_iface);
  return 0;
}
