
#include <newt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

// Global variable for the background hotspot process.
pid_t hotspot_pid = -1;

/* Function to start the hotspot. It forks and execs a daemon.
   Ensure your hotspot daemon is compiled and available at the specified path.
 */
void start_hotspot(void) {
  if (hotspot_pid > 0) {
    newtWinMessage("Hotspot", "Already Running", "Hotspot is already running.");
    return;
  }
  hotspot_pid = fork();
  if (hotspot_pid < 0) {
    newtWinMessage("Error", "Fork Failed", "Failed to fork hotspot process.");
  } else if (hotspot_pid == 0) {
    // Child process: detach and execute the hotspot daemon.
    setsid();
    execl("/usr/local/bin/hotspot_daemon", "hotspot_daemon", (char *)NULL);
    perror("execl");
    exit(1);
  } else {
    newtWinMessage("Hotspot", "Started", "Hotspot started in background.");
  }
}

/* Function to stop the hotspot by sending SIGTERM */
void stop_hotspot(void) {
  if (hotspot_pid > 0) {
    if (kill(hotspot_pid, SIGTERM) == 0) {
      waitpid(hotspot_pid, NULL, 0);
      newtWinMessage("Hotspot", "Stopped", "Hotspot stopped.");
      hotspot_pid = -1;
    } else {
      newtWinMessage("Hotspot", "Error", "Failed to stop hotspot.");
    }
  } else {
    newtWinMessage("Hotspot", "Not Running", "Hotspot is not running.");
  }
}

/* Stub function for configuring the hotspot */
void config_hotspot(void) {
  newtWinMessage("Hotspot", "Config", "Hotspot configuration not implemented.");
}

int main(void) {
  newtComponent btnStart, btnStop, btnConfig, btnQuit, form;
  struct newtExitStruct es;

  // Initialize newt and clear the screen.
  newtInit();
  newtCls();

  // Do not create a centered window (which adds a white background and shadow).
  // Instead, simply create a form directly.
  form = newtForm(NULL, NULL, 0);

  // Create four buttons at specific coordinates (x,y) with enough vertical gap.
  btnStart = newtButton(10, 3, "Start Hotspot");
  btnStop = newtButton(10, 6, "Stop Hotspot");
  btnConfig = newtButton(10, 9, "Config Hotspot");
  btnQuit = newtButton(10, 12, "Quit");

  // Add the buttons to the form.
  newtFormAddComponents(form, btnStart, btnStop, btnConfig, btnQuit, NULL);

  // Main event loop.
  while (1) {
    newtFormRun(form, &es);
    if (es.u.co == btnStart) {
      start_hotspot();
    } else if (es.u.co == btnStop) {
      stop_hotspot();
    } else if (es.u.co == btnConfig) {
      config_hotspot();
    } else if (es.u.co == btnQuit) {
      // Quit the UI without stopping any running hotspot process.
      break;
    }
  }

  newtFinished();
  return 0;
}
