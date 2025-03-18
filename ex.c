#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define AP_IFACE "ap0"
#define HOSTAPD_CONF "/tmp/hostapd.conf"

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

  // Check prerequisites.
  const char *prereqs[] = {"iw", "hostapd", "dnsmasq", "nmcli"};
  for (int i = 0; i < 4; i++) {
    char checkCmd[128];
    snprintf(checkCmd, sizeof(checkCmd), "command -v %s >/dev/null 2>&1",
             prereqs[i]);
    if (system(checkCmd) != 0) {
      fprintf(stderr, "%s required\n", prereqs[i]);
      exit(1);
    }
  }

  // Automatically fetch WLAN interface from OS.
  char *wlan_iface =
      exec_cmd("iw dev | grep Interface | awk '{print $2}' | head -n1");
  if (!wlan_iface || strlen(wlan_iface) == 0) {
    fprintf(stderr, "No WLAN interface detected.\n");
    exit(1);
  }
  // Remove any trailing newline.
  wlan_iface[strcspn(wlan_iface, "\n")] = '\0';
  printf("Detected WLAN interface: %s\n", wlan_iface);

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
  printf("Starting NetworkManager...\n");
  system("sudo systemctl start NetworkManager");
  if (system("systemctl is-active NetworkManager >/dev/null 2>&1") != 0) {
    fprintf(stderr, "NetworkManager failed to start\n");
    exit(1);
  }

  // Verify primary wireless connection via nmcli.
  printf("Checking %s connection...\n", wlan_iface);
  char nmcliCmd[256];
  snprintf(
      nmcliCmd, sizeof(nmcliCmd),
      "nmcli -t -f NAME,DEVICE con show --active | grep \"%s\" | cut -d: -f1",
      wlan_iface);
  char *connection = exec_cmd(nmcliCmd);
  if (!connection || strlen(connection) == 0) {
    fprintf(stderr, "Error: %s not connected.\n", wlan_iface);
    system("nmcli dev status");
    exit(1);
  }
  printf("Connected via: %s", connection);

  // Extract channel and frequency info using iw.
  char iwCmd[256];
  snprintf(iwCmd, sizeof(iwCmd), "iw dev %s info", wlan_iface);
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
  snprintf(checkAP, sizeof(checkAP), "sudo iw dev %s info >/dev/null 2>&1",
           AP_IFACE);
  if (system(checkAP) == 0) {
    printf("Interface %s already exists. Removing it...\n", AP_IFACE);
    char delCmd[128];
    snprintf(delCmd, sizeof(delCmd), "sudo iw dev %s del", AP_IFACE);
    system(delCmd);
  }

  // Create the AP interface.
  printf("Creating %s...\n", AP_IFACE);
  char addIf[256];
  snprintf(addIf, sizeof(addIf), "sudo iw dev %s interface add %s type __ap",
           wlan_iface, AP_IFACE);
  if (system(addIf) != 0) {
    fprintf(stderr, "Failed to create AP interface %s\n", AP_IFACE);
    exit(1);
  }
  char nmcliSet[128];
  snprintf(nmcliSet, sizeof(nmcliSet), "sudo nmcli dev set %s managed no",
           AP_IFACE);
  system(nmcliSet);

  // Check internet connectivity.
  printf("Checking internet connectivity...\n");
  if (system("ping -c 2 8.8.8.8 >/dev/null 2>&1") != 0) {
    printf("Internet appears down; attempting to reconnect...\n");
    char upConn[256];
    snprintf(upConn, sizeof(upConn), "sudo nmcli con up \"%s\"", connection);
    system(upConn);
    sleep(2);
    if (system("ping -c 2 8.8.8.8 >/dev/null 2>&1") != 0) {
      fprintf(stderr, "Failed to restore internet connection.\n");
      char delCmd2[128];
      snprintf(delCmd2, sizeof(delCmd2), "sudo iw dev %s del", AP_IFACE);
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

  if (system("pgrep dnsmasq >/dev/null 2>&1") == 0) {
    printf("Stopping existing dnsmasq...\n");
    system("sudo killall dnsmasq");
  }

  // Start hostapd.
  printf("Starting hostapd...\n");
  hostapd_pid = fork();
  if (hostapd_pid == 0) {
    execlp("sudo", "sudo", "hostapd", HOSTAPD_CONF, NULL);
    perror("execlp hostapd failed");
    exit(1);
  }
  sleep(2);
  if (kill(hostapd_pid, 0) != 0) {
    fprintf(stderr, "hostapd failed to start. Configuration:\n");
    system("cat " HOSTAPD_CONF);
    char delCmd3[128];
    snprintf(delCmd3, sizeof(delCmd3), "sudo iw dev %s del", AP_IFACE);
    system(delCmd3);
    exit(1);
  }

  printf("Setting up IP and DHCP for %s...\n", AP_IFACE);
  char ipCmd[128];
  snprintf(ipCmd, sizeof(ipCmd), "sudo ip addr add 192.168.4.1/24 dev %s",
           AP_IFACE);
  system(ipCmd);
  char linkCmd[128];
  snprintf(linkCmd, sizeof(linkCmd), "sudo ip link set %s up", AP_IFACE);
  system(linkCmd);
  char dnsCmd[256];
  snprintf(dnsCmd, sizeof(dnsCmd),
           "sudo dnsmasq --interface=%s "
           "--dhcp-range=192.168.4.2,192.168.4.100,12h &",
           AP_IFACE);
  system(dnsCmd);

  printf("Enabling NAT...\n");
  system("sudo sysctl -w net.ipv4.ip_forward=1");
  char iptCmd[256];
  snprintf(iptCmd, sizeof(iptCmd),
           "sudo iptables -t nat -A POSTROUTING -o %s -j MASQUERADE",
           wlan_iface);
  system(iptCmd);
  snprintf(iptCmd, sizeof(iptCmd),
           "sudo iptables -A FORWARD -i %s -o %s -j ACCEPT", AP_IFACE,
           wlan_iface);
  system(iptCmd);
  snprintf(iptCmd, sizeof(iptCmd),
           "sudo iptables -A FORWARD -i %s -o %s -m state --state "
           "RELATED,ESTABLISHED -j ACCEPT",
           wlan_iface, AP_IFACE);
  system(iptCmd);

  printf("Hotspot started on channel %s using interface %s. Press Ctrl+C to "
         "stop.\n",
         channel, AP_IFACE);

  while (1) {
    pause();
  }

  free(wlan_iface);
  return 0;
}
