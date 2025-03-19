
/*
 * hotspot.c
 *
 * This program sets up a Wi-Fi hotspot on Linux using hostapd and dnsmasq.
 *
 * Requirements:
 *   - Linux with installed commands: iw, hostapd, dnsmasq, nmcli, ip, iptables,
 * systemctl.
 *   - Must be run with sufficient privileges (e.g., via sudo) to execute
 * system-level commands.
 *
 * Compilation: gcc -o hotspot hotspot.c
 */

#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define WLAN_IFACE "wlp0s20f3" // Primary Wi-Fi interface (adjust as needed)
#define AP_IFACE "ap0"         // Virtual interface for hotspot
#define HOSTAPD_CONF "/tmp/hostapd.conf"
#define BUFFER_SIZE 1024

volatile pid_t hostapd_pid = -1;
volatile pid_t dnsmasq_pid = -1;

/* Cleanup function to stop child processes and remove AP interface */
void cleanup(int signo) {
  printf("\nStopping hotspot...\n");
  if (hostapd_pid > 0) {
    kill(hostapd_pid, SIGTERM);
    waitpid(hostapd_pid, NULL, 0);
  }
  if (dnsmasq_pid > 0) {
    kill(dnsmasq_pid, SIGTERM);
    waitpid(dnsmasq_pid, NULL, 0);
  }
  system("sudo iw dev " AP_IFACE " del");
  exit(0);
}

/* Helper: Check if a command exists by using 'command -v' */
int check_command(const char *cmd) {
  char check_cmd[256];
  snprintf(check_cmd, sizeof(check_cmd), "command -v %s >/dev/null 2>&1", cmd);
  return system(check_cmd);
}

/* Helper: Execute a command and capture its output */
int exec_cmd(const char *cmd, char *output, size_t out_size) {
  FILE *fp = popen(cmd, "r");
  if (!fp)
    return -1;
  if (fgets(output, out_size, fp) == NULL) {
    pclose(fp);
    return -1;
  }
  pclose(fp);
  return 0;
}

/* Helper: Trim newline and spaces */
void trim(char *str) {
  char *end;
  while (*str == ' ' || *str == '\t')
    str++;
  end = str + strlen(str) - 1;
  while (end > str && (*end == '\n' || *end == ' ' || *end == '\t')) {
    *end = '\0';
    end--;
  }
}

/* Parse channel and frequency from iw output */
int parse_channel_info(const char *iface, int *channel, int *freq) {
  char cmd[256];
  char buffer[BUFFER_SIZE];
  FILE *fp;
  snprintf(cmd, sizeof(cmd), "iw dev %s info", iface);
  fp = popen(cmd, "r");
  if (!fp)
    return -1;
  int found = 0;
  while (fgets(buffer, sizeof(buffer), fp)) {
    if (strstr(buffer, "channel")) {
      // Example expected line:
      // "    channel 36 (5180 MHz), width: 80 MHz, center1: 5210 MHz"
      char *p = strstr(buffer, "channel");
      if (p) {
        int ch, fr;
        if (sscanf(p, "channel %d (%d", &ch, &fr) == 2) {
          *channel = ch;
          *freq = fr;
          found = 1;
          break;
        }
      }
    }
  }
  pclose(fp);
  return found ? 0 : -1;
}

/* Fork and execute a command in the background. Returns child PID or -1 on
 * error. */
pid_t start_process(char *const argv[]) {
  pid_t pid = fork();
  if (pid == 0) {
    // Child: execute the command (using sudo when needed)
    execvp(argv[0], argv);
    perror("execvp failed");
    exit(1);
  } else if (pid < 0) {
    perror("fork failed");
    return -1;
  }
  return pid;
}

int main(void) {
  struct rlimit rl;
  int ret;
  char buffer[BUFFER_SIZE];

  /* Ensure the program is run with root privileges */
  if (geteuid() != 0) {
    fprintf(stderr, "This program must be run as root. Please use sudo.\n");
    exit(1);
  }

  /* Increase file descriptor limit */
  rl.rlim_cur = rl.rlim_max = 4096;
  if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
    perror("setrlimit failed");
    // Not fatal; continue
  }

  /* Check prerequisites */
  const char *prereqs[] = {"iw", "hostapd", "dnsmasq", "nmcli", NULL};
  for (int i = 0; prereqs[i] != NULL; i++) {
    if (check_command(prereqs[i]) != 0) {
      fprintf(stderr,
              "%s is required but not installed. Please install it and try "
              "again.\n",
              prereqs[i]);
      exit(1);
    }
  }

  /* Get SSID and password from user */
  char ssid[128];
  char *pass;
  printf("Enter SSID for hotspot: ");
  if (!fgets(ssid, sizeof(ssid), stdin)) {
    fprintf(stderr, "Failed to read SSID\n");
    exit(1);
  }
  trim(ssid);
  /* For hidden password input, use getpass() */
  pass = getpass("Enter Password for hotspot: ");
  if (!pass || strlen(pass) < 8) {
    fprintf(stderr,
            "Password is required and should be at least 8 characters.\n");
    exit(1);
  }

  /* Start NetworkManager */
  printf("Starting NetworkManager...\n");
  ret = system("sudo systemctl start NetworkManager");
  if (ret != 0) {
    fprintf(stderr, "Failed to start NetworkManager.\n");
    exit(1);
  }
  ret = system("systemctl is-active NetworkManager >/dev/null 2>&1");
  if (ret != 0) {
    fprintf(stderr, "NetworkManager failed to start\n");
    exit(1);
  }

  /* Check primary wireless connection via nmcli */
  printf("Checking " WLAN_IFACE " connection...\n");
  FILE *fp = popen("nmcli -t -f NAME,DEVICE con show --active", "r");
  if (!fp) {
    perror("popen nmcli");
    exit(1);
  }
  char connection[128] = {0};
  while (fgets(buffer, sizeof(buffer), fp))
  ::contentReference [oaicite:0]{index = 0}
