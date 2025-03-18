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

// Cleanup function to be called on SIGINT/SIGTERM.
void cleanup(int sig) {
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
  signal(SIGINT, cleanup);
  signal(SIGTERM, cleanup);

  // Increase file descriptor limit.
  struct rlimit rl;
  if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
    rl.rlim_cur = 4096;
    setrlimit(RLIMIT_NOFILE, &rl);
  }

  // Fetch full paths for all required commands.
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

  // Automatically fetch the connected WLAN interface using nmcli.
  char *wlan_iface = exec_cmd("nmcli -t -f DEVICE,TYPE,STATE dev status | grep "
                              "':wifi:connected' | cut -d: -f1 | head -n1");
  if (!wlan_iface || strlen(wlan_iface) == 0) {
    fprintf(stderr, "No connected WLAN interface detected.\n");
    exit(1);
  }
  wlan_iface[strcspn(wlan_iface, "\n")] = '\0';
  printf("Detected connected WLAN interface: %s\n", wlan_iface);

  // Prompt user for SSID and Password.
  char ssid[128], pass[128];
  printf("Enter SSID for hotspot: ");
  if (!fgets(ssid, sizeof(ssid), stdin)) {
    fprintf(stderr, "Error reading SSID\n");
    exit(1);
  }
  ssid[strcspn(ssid, "\n")] = '\0';

  printf("Enter Password for hotspot: ");
  if (!fgets(pass, sizeof(pass), stdin)) {
    fprintf(stderr, "Error reading Password\n");
    exit(1);
  }
  pass[strcspn(pass, "\n")] = '\0';
  printf("\n");

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

  int freqVal = atoi(freq);
  if (freqVal < 5000) {
    fprintf(stderr, "Error: Primary connection is on 2.4 GHz. Please use a 5 "
                    "GHz network or a separate adapter for the hotspot.\n");
    exit(1);
  }

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

  // Check internet connectivity.
  printf("Checking internet connectivity...\n");
  if (system("ping -c 2 8.8.8.8 >/dev/null 2>&1") != 0) {
    char upConn[256];
    snprintf(upConn, sizeof(upConn), "sudo %s con up \"%s\"", nmcli_path,
             connection);
    printf("Internet appears down; attempting to reconnect...\n");
    system(upConn);
    sleep(2);
    if (system("ping -c 2 8.8.8.8 >/dev/null 2>&1") != 0) {
      fprintf(stderr, "Failed to restore internet connection.\n");
      char delCmd2[128];
      snprintf(delCmd2, sizeof(delCmd2), "sudo %s dev %s del", iw_path,
               AP_IFACE);
      system(delCmd2);
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
  sleep(2);
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

  // Check that the AP interface has the correct IP.
  if (!check_ap_ip(ip_path)) {
    fprintf(stderr, "AP interface %s did not receive the correct IP address.\n",
            AP_IFACE);
    exit(1);
  }

  // Start dnsmasq for DHCP.
  char dnsCmd[256];
  snprintf(dnsCmd, sizeof(dnsCmd), "sudo %s --interface=%s --dhcp-range=%s &",
           dnsmasq_path, AP_IFACE, DHCP_RANGE);
  system(dnsCmd);
  sleep(2);
  if (!check_dnsmasq_running(dnsmasq_path)) {
    fprintf(stderr, "dnsmasq is not running. DHCP will not work.\n");
    exit(1);
  } else {
    printf("dnsmasq is running and DHCP is enabled.\n");
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
    pause();
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
