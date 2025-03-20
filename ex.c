#include <ncurses.h>
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
#define CONFIG_FILE "/tmp/hotspot.conf" // File to persist SSID and password

// Global process IDs.
pid_t hostapd_pid = -1; // For hostapd process
pid_t hotspot_pid = -1; // For the overall hotspot process

// --- Helper Functions ---

// Execute a command and capture its output.
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

// Get the full path of a command.
char *get_cmd_path(const char *cmd) {
  char buf[256];
  snprintf(buf, sizeof(buf), "command -v %s", cmd);
  char *path = exec_cmd(buf);
  if (path) {
    path[strcspn(path, "\n")] = '\0';
  }
  return path;
}

int check_dnsmasq_running(const char *dnsmasq_path) {
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "pgrep -x %s >/dev/null 2>&1", "dnsmasq");
  return (system(cmd) == 0);
}

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

void check_systemd_resolved() {
  if (system("systemctl is-active --quiet systemd-resolved") == 0) {
    fprintf(stderr, "Warning: systemd-resolved is active. It may conflict with "
                    "dnsmasq on port 53.\n");
  }
}

// Load hotspot configuration (SSID and password) from file.
void load_hotspot_config(char *ssid, size_t ssid_len, char *pass,
                         size_t pass_len) {
  FILE *config = fopen(CONFIG_FILE, "r");
  if (config) {
    if (fgets(ssid, ssid_len, config) != NULL)
      ssid[strcspn(ssid, "\n")] = '\0';
    if (fgets(pass, pass_len, config) != NULL)
      pass[strcspn(pass, "\n")] = '\0';
    fclose(config);
  } else {
    strncpy(ssid, "MyHotspot", ssid_len);
    strncpy(pass, "password123", pass_len);
  }
}

// Cleanup function for the hotspot process.
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

// --- Hotspot Process Function ---
void run_hotspot() {
  signal(SIGINT, cleanup_handler);
  signal(SIGTERM, cleanup_handler);

  struct rlimit rl;
  if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
    rl.rlim_cur = 4096;
    setrlimit(RLIMIT_NOFILE, &rl);
  }

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

  char *wlan_iface = exec_cmd("nmcli -t -f DEVICE,TYPE,STATE dev status | grep "
                              "':wifi:connected' | cut -d: -f1 | head -n1");
  if (!wlan_iface || strlen(wlan_iface) == 0) {
    fprintf(stderr, "No connected WLAN interface detected.\n");
    exit(1);
  }
  wlan_iface[strcspn(wlan_iface, "\n")] = '\0';
  printf("Detected connected WLAN interface: %s\n", wlan_iface);

  char ssid[128], pass[128];
  load_hotspot_config(ssid, sizeof(ssid), pass, sizeof(pass));
  printf("Using hotspot configuration: SSID=%s\n", ssid);

  int check_interval = 10;
  printf("Using connectivity check interval: %d seconds\n", check_interval);

  char nmcli_start[256];
  snprintf(nmcli_start, sizeof(nmcli_start), "sudo %s start NetworkManager",
           systemctl_path);
  printf("Starting NetworkManager...\n");
  system(nmcli_start);
  if (system("systemctl is-active NetworkManager >/dev/null 2>&1") != 0) {
    fprintf(stderr, "NetworkManager failed to start\n");
    exit(1);
  }

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

  // --- Modified: Fetch channel and frequency, determine GHz band and hw_mode
  // ---
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
        // Expected line: " channel 36 (5180 MHz)"
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

  int freqVal = atoi(freq);
  // Determine hardware mode based on frequency.
  // If freq < 5000, assume 2.4 GHz and use "g", otherwise 5 GHz with "a".
  const char *hw_mode = (freqVal < 5000) ? "g" : "a";
  printf("Using hardware mode: %s\n", hw_mode);

  // Additional edge-case handling: Validate the channel number.
  if (hw_mode[0] == 'g') {
    // For 2.4 GHz, valid channels are typically 1-14.
    int ch = atoi(channel);
    if (ch < 1 || ch > 14) {
      fprintf(stderr,
              "Detected 2.4 GHz channel %d is out of expected range (1-14). "
              "Defaulting to channel 6.\n",
              ch);
      strncpy(channel, "6", sizeof(channel) - 1);
    }
  } else {
    // For 5 GHz, valid channels are usually between 36 and 165.
    int ch = atoi(channel);
    if (ch < 36 || ch > 165) {
      fprintf(stderr,
              "Detected 5 GHz channel %d is out of expected range (36-165). "
              "Defaulting to channel 36.\n",
              ch);
      strncpy(channel, "36", sizeof(channel) - 1);
    }
  }
  printf("Hotspot will be created on channel %s (%s band).\n", channel,
         (hw_mode[0] == 'g') ? "2.4 GHz" : "5 GHz");
  // --- End Modified Section ---

  free(connection);

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

  printf("Checking internet connectivity...\n");
  if (system("ping -c 2 google.com >/dev/null 2>&1") != 0) {
    if (auto_switch_wifi(nmcli_path) != 0) {
      fprintf(stderr, "Initial reconnection failed.\n");
      exit(1);
    }
  }

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

  if (system("pgrep dnsmasq >/dev/null 2>&1") == 0) {
    printf("Stopping existing dnsmasq...\n");
    system("sudo killall dnsmasq");
  }

  printf("Starting hostapd...\n");
  hostapd_pid = fork();
  if (hostapd_pid == 0) {
    // Redirect child's stdout and stderr to /dev/null to avoid interfering with
    // the TUI.
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
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
  printf("Press Ctrl+C to stop hotspot.\n");

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
  exit(0);
}

// --- TUI Functions ---

// Display and update hotspot configuration.
void configure_hotspot_tui() {
  char ssid[128], pass[128];
  load_hotspot_config(ssid, sizeof(ssid), pass, sizeof(pass));

  clear();
  box(stdscr, 0, 0);
  mvprintw(1, 2, "=== Configure Hotspot ===");
  mvprintw(3, 2, "Current SSID: %s", ssid);
  mvprintw(4, 2, "Current Password: %s", pass);

  mvprintw(6, 2, "Enter new SSID (leave blank to keep current): ");
  echo();
  char new_ssid[128];
  getnstr(new_ssid, sizeof(new_ssid) - 1);
  noecho();
  if (strlen(new_ssid) > 0) {
    strncpy(ssid, new_ssid, sizeof(ssid) - 1);
  }

  mvprintw(8, 2, "Enter new Password (leave blank to keep current): ");
  echo();
  char new_pass[128];
  getnstr(new_pass, sizeof(new_pass) - 1);
  noecho();
  if (strlen(new_pass) > 0) {
    strncpy(pass, new_pass, sizeof(pass) - 1);
  }

  FILE *config = fopen(CONFIG_FILE, "w");
  if (config) {
    fprintf(config, "%s\n%s\n", ssid, pass);
    fclose(config);
    mvprintw(10, 2, "Hotspot configuration updated!");
  } else {
    mvprintw(10, 2, "Error updating configuration!");
  }
  mvprintw(12, 2, "Press any key to return to menu...");
  refresh();
  getch();
}

// Start the hotspot process.
void start_hotspot_tui() {
  if (hotspot_pid > 0) {
    clear();
    box(stdscr, 0, 0);
    attron(COLOR_PAIR(2));
    mvprintw(2, 2, "Hotspot is already running (PID: %d).", hotspot_pid);
    attroff(COLOR_PAIR(2));
    mvprintw(4, 2, "Press any key to return to menu...");
    refresh();
    getch();
    return;
  }
  hotspot_pid = fork();
  if (hotspot_pid == 0) {
    // In child: redirect output and run hotspot.
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    run_hotspot();
    exit(0);
  } else if (hotspot_pid < 0) {
    clear();
    mvprintw(2, 2, "Failed to start hotspot.");
    refresh();
    getch();
  } else {
    clear();
    attron(COLOR_PAIR(2));
    mvprintw(2, 2, "Hotspot started successfully (PID: %d).", hotspot_pid);
    attroff(COLOR_PAIR(2));
    mvprintw(4, 2, "Press any key to return to menu...");
    refresh();
    getch();
  }
}

// Stop the hotspot process.
void stop_hotspot_tui() {
  if (hotspot_pid <= 0) {
    clear();
    box(stdscr, 0, 0);
    mvprintw(2, 2, "Hotspot is not running.");
    mvprintw(4, 2, "Press any key to return to menu...");
    refresh();
    getch();
    return;
  }
  kill(hotspot_pid, SIGTERM);
  waitpid(hotspot_pid, NULL, 0);
  clear();
  box(stdscr, 0, 0);
  mvprintw(2, 2, "Hotspot stopped successfully.");
  hotspot_pid = -1;
  mvprintw(4, 2, "Press any key to return to menu...");
  refresh();
  getch();
}

// Parent process signal handler for Ctrl+C.
void parent_sigint_handler(int sig) {
  if (hotspot_pid > 0) {
    kill(hotspot_pid, SIGTERM);
    waitpid(hotspot_pid, NULL, 0);
    hotspot_pid = -1;
  }
  endwin();
  exit(0);
}

// --- Main TUI Loop ---
int main() {
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);

  // Set parent process signal handler for Ctrl+C.
  signal(SIGINT, parent_sigint_handler);

  if (has_colors()) {
    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLUE);   // Border and title
    init_pair(2, COLOR_YELLOW, COLOR_BLACK); // Status messages
    init_pair(3, COLOR_GREEN, COLOR_BLACK);  // Menu highlight
  }

  const char *menu_items[] = {"Start Hotspot", "Stop Hotspot",
                              "Configure Hotspot", "Exit"};
  int num_items = sizeof(menu_items) / sizeof(menu_items[0]);
  int highlight = 0;
  int choice;
  int ch;

  while (1) {
    clear();
    // Draw border with title
    attron(COLOR_PAIR(1));
    box(stdscr, 0, 0);
    mvprintw(0, 3, " WiFi & Hotspot Manager ");
    attroff(COLOR_PAIR(1));

    // Draw menu items in a grid style (centered)
    for (int i = 0; i < num_items; i++) {
      int y = 3 + i * 2;
      int x = 4;
      if (i == highlight) {
        attron(A_REVERSE | COLOR_PAIR(3));
        mvprintw(y, x, "%s", menu_items[i]);
        attroff(A_REVERSE | COLOR_PAIR(3));
      } else {
        mvprintw(y, x, "%s", menu_items[i]);
      }
    }
    refresh();

    ch = getch();
    switch (ch) {
    case KEY_UP:
      highlight--;
      if (highlight < 0)
        highlight = num_items - 1;
      break;
    case KEY_DOWN:
      highlight++;
      if (highlight >= num_items)
        highlight = 0;
      break;
    case 10: // Enter key
      choice = highlight;
      if (choice == 0) { // Start Hotspot
        start_hotspot_tui();
      } else if (choice == 1) { // Stop Hotspot
        stop_hotspot_tui();
      } else if (choice == 2) { // Configure Hotspot
        configure_hotspot_tui();
      } else if (choice == 3) { // Exit
        if (hotspot_pid > 0) {
          clear();
          box(stdscr, 0, 0);
          mvprintw(2, 2, "Hotspot is still running (PID: %d).", hotspot_pid);
          mvprintw(4, 2, "Stop hotspot and exit? (y/n): ");
          refresh();
          int confirm = getch();
          if (confirm == 'y' || confirm == 'Y') {
            kill(hotspot_pid, SIGTERM);
            waitpid(hotspot_pid, NULL, 0);
            hotspot_pid = -1;
            endwin();
            exit(0);
          }
        } else {
          endwin();
          exit(0);
        }
      }
      break;
    default:
      break;
    }
  }
  endwin();
  return 0;
}
