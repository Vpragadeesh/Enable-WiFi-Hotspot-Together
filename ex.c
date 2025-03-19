
#include <fcntl.h>
#include <form.h>
#include <menu.h>
#include <ncurses.h>
#include <stdbool.h> // Include this for bool type
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// ----------------------------
// Configuration & Definitions
// ----------------------------
#define CONFIG_FILE "/tmp/hotspot.conf"
#define SSID_LEN 40
#define PASS_LEN 40
#define INTERVAL_LEN 8

#define WIN_WIDTH 60
#define WIN_HEIGHT 18
#define NUM_CHOICES 4

// Global variable to track hotspot status and process.
bool hotspot_running = false; // Now declared as a global bool.
pid_t hotspot_pid = 0;
int hotspot_fd = -1; // File descriptor for reading hotspot output (if needed)

// Global configuration structure.
typedef struct {
  char ssid[SSID_LEN];
  char pass[PASS_LEN];
  int interval; // connectivity check interval in seconds
} HotspotConfig;

// ----------------------------
// Color Initialization
// ----------------------------
void init_colors() {
  start_color();
  init_pair(1, COLOR_WHITE, COLOR_BLUE);   // Header
  init_pair(2, COLOR_YELLOW, COLOR_BLACK); // Highlight
  init_pair(3, COLOR_GREEN, COLOR_BLACK);  // Success
  init_pair(4, COLOR_CYAN, COLOR_BLACK);   // Normal text
  init_pair(5, COLOR_RED, COLOR_BLACK);    // Error
}

// ----------------------------
// Hotspot Control Functions
// ----------------------------
void start_hotspot() {
  if (hotspot_running)
    return;
  HotspotConfig config;
  FILE *fp = fopen(CONFIG_FILE, "r");
  if (fp) {
    if (fgets(config.ssid, SSID_LEN, fp) == NULL) {
      fclose(fp);
      return;
    }
    config.ssid[strcspn(config.ssid, "\n")] = '\0';
    if (fgets(config.pass, PASS_LEN, fp) == NULL) {
      fclose(fp);
      return;
    }
    config.pass[strcspn(config.pass, "\n")] = '\0';
    char interval_str[INTERVAL_LEN] = {0};
    if (fgets(interval_str, INTERVAL_LEN, fp) != NULL) {
      config.interval = atoi(interval_str);
      if (config.interval <= 0)
        config.interval = 10;
    } else {
      config.interval = 10;
    }
    fclose(fp);
  } else {
    config.interval = 10;
  }
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    attron(COLOR_PAIR(5));
    mvprintw(LINES - 2, 2, "Error: Unable to create pipe.");
    attroff(COLOR_PAIR(5));
    refresh();
    return;
  }
  hotspot_pid = fork();
  if (hotspot_pid < 0) {
    attron(COLOR_PAIR(5));
    mvprintw(LINES - 2, 2, "Error: Fork failed.");
    attroff(COLOR_PAIR(5));
    refresh();
    return;
  }
  if (hotspot_pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    char interval_arg[INTERVAL_LEN];
    snprintf(interval_arg, sizeof(interval_arg), "%d", config.interval);
    execlp("sudo", "sudo", "./myc", interval_arg, NULL);
    perror("execlp failed");
    exit(1);
  }
  close(pipefd[1]);
  hotspot_fd = pipefd[0];
  hotspot_running = true;
}

void stop_hotspot() {
  if (!hotspot_running)
    return;
  if (kill(hotspot_pid, SIGTERM) == 0) {
    waitpid(hotspot_pid, NULL, 0);
  }
  hotspot_pid = 0;
  if (hotspot_fd != -1) {
    close(hotspot_fd);
    hotspot_fd = -1;
  }
  hotspot_running = false;
}

void display_hotspot_log() {
  WINDOW *log_win =
      newwin(WIN_HEIGHT - 4, WIN_WIDTH - 4, (LINES - (WIN_HEIGHT - 4)) / 2,
             (COLS - (WIN_WIDTH - 4)) / 2);
  scrollok(log_win, TRUE);
  nodelay(log_win, TRUE);
  box(log_win, 0, 0);
  attron(COLOR_PAIR(1) | A_BOLD);
  mvwprintw(log_win, 0, 2, "Hotspot Log (press 'q' to exit)");
  attroff(COLOR_PAIR(1) | A_BOLD);
  wrefresh(log_win);
  char buf[256];
  int ch;
  FILE *fp = fdopen(hotspot_fd, "r");
  if (!fp) {
    mvwprintw(log_win, 2, 2, "Error reading hotspot output.");
    wrefresh(log_win);
    getch();
    delwin(log_win);
    return;
  }
  while ((ch = wgetch(log_win)) != 'q') {
    if (fgets(buf, sizeof(buf), fp) != NULL) {
      waddstr(log_win, buf);
      wrefresh(log_win);
    }
    napms(100);
  }
  delwin(log_win);
}

// ----------------------------
// Configuration Functions
// ----------------------------
void configure_hotspot() {
  echo();
  HotspotConfig config;
  memset(&config, 0, sizeof(config));
  clear();
  attron(COLOR_PAIR(1) | A_BOLD);
  mvprintw(2, 2, "Configure Hotspot");
  attroff(COLOR_PAIR(1) | A_BOLD);
  attron(COLOR_PAIR(4));
  mvprintw(4, 2, "Enter SSID: ");
  getnstr(config.ssid, SSID_LEN - 1);
  mvprintw(6, 2, "Enter Password: ");
  getnstr(config.pass, PASS_LEN - 1);
  mvprintw(8, 2, "Enter connectivity check interval (sec) [default 10]: ");
  char interval_str[INTERVAL_LEN] = {0};
  getnstr(interval_str, INTERVAL_LEN - 1);
  attroff(COLOR_PAIR(4));
  config.interval = atoi(interval_str);
  if (config.interval <= 0)
    config.interval = 10;
  FILE *fp = fopen(CONFIG_FILE, "w");
  if (fp) {
    fprintf(fp, "%s\n%s\n%d\n", config.ssid, config.pass, config.interval);
    fclose(fp);
  }
  attron(COLOR_PAIR(3));
  mvprintw(10, 2, "Configuration saved! Press any key to return.");
  attroff(COLOR_PAIR(3));
  noecho();
  getch();
}

// ----------------------------
// TUI Menu using ncurses menu library
// ----------------------------
int main() {
  ITEM **items;
  MENU *my_menu;
  int n_choices, i, c;
  char *choices[] = {"Start Hotspot", "Stop Hotspot", "Configure Hotspot",
                     "Quit", NULL};
  n_choices = 4;
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  init_colors();
  items = (ITEM **)calloc(n_choices + 1, sizeof(ITEM *));
  for (i = 0; i < n_choices; i++) {
    items[i] = new_item(choices[i], "");
  }
  items[n_choices] = NULL;
  my_menu = new_menu((ITEM **)items);
  set_menu_mark(my_menu, " > ");
  WINDOW *menu_win = newwin(WIN_HEIGHT, WIN_WIDTH, (LINES - WIN_HEIGHT) / 2,
                            (COLS - WIN_WIDTH) / 2);
  keypad(menu_win, TRUE);
  box(menu_win, 0, 0);
  attron(COLOR_PAIR(1) | A_BOLD);
  mvwprintw(menu_win, 1, (WIN_WIDTH - 26) / 2, "Hotspot Manager (TUI)");
  attroff(COLOR_PAIR(1) | A_BOLD);
  mvwprintw(menu_win, 2, 2, "Use arrow keys and Enter to select");
  set_menu_win(my_menu, menu_win);
  set_menu_sub(my_menu, derwin(menu_win, 4, WIN_WIDTH - 4, 4, 2));
  post_menu(my_menu);
  wrefresh(menu_win);

  while ((c = wgetch(menu_win)) != 'q') {
    switch (c) {
    case KEY_DOWN:
      menu_driver(my_menu, REQ_DOWN_ITEM);
      break;
    case KEY_UP:
      menu_driver(my_menu, REQ_UP_ITEM);
      break;
    case 10: { // Enter key
      ITEM *cur = current_item(my_menu);
      const char *choice = item_name(cur);
      mvwprintw(menu_win, WIN_HEIGHT - 2, 2,
                "                                        ");
      wrefresh(menu_win);
      if (strcmp(choice, "Start Hotspot") == 0) {
        if (!hotspot_running) {
          start_hotspot();
          display_hotspot_log();
        }
      } else if (strcmp(choice, "Stop Hotspot") == 0) {
        if (hotspot_running) {
          stop_hotspot();
          attron(COLOR_PAIR(3));
          mvwprintw(menu_win, WIN_HEIGHT - 2, 2,
                    "Hotspot stopped. Press any key.");
          attroff(COLOR_PAIR(3));
          wrefresh(menu_win);
          wgetch(menu_win);
        }
      } else if (strcmp(choice, "Configure Hotspot") == 0) {
        unpost_menu(my_menu);
        free_menu(my_menu);
        for (i = 0; i < n_choices; i++)
          free_item(items[i]);
        clear();
        refresh();
        configure_hotspot();
        // Rebuild menu after configuration.
        items = (ITEM **)calloc(n_choices + 1, sizeof(ITEM *));
        items[0] = new_item("Start Hotspot", "");
        items[1] = new_item("Stop Hotspot", "");
        items[2] = new_item("Configure Hotspot", "");
        items[3] = new_item("Quit", "");
        items[4] = NULL;
        my_menu = new_menu((ITEM **)items);
        set_menu_mark(my_menu, " > ");
        menu_win = newwin(WIN_HEIGHT, WIN_WIDTH, (LINES - WIN_HEIGHT) / 2,
                          (COLS - WIN_WIDTH) / 2);
        keypad(menu_win, TRUE);
        box(menu_win, 0, 0);
        attron(COLOR_PAIR(1) | A_BOLD);
        mvwprintw(menu_win, 1, (WIN_WIDTH - 26) / 2, "Hotspot Manager (TUI)");
        attroff(COLOR_PAIR(1) | A_BOLD);
        mvwprintw(menu_win, 2, 2, "Use arrow keys and Enter to select");
        set_menu_win(my_menu, menu_win);
        set_menu_sub(my_menu, derwin(menu_win, 4, WIN_WIDTH - 4, 4, 2));
        post_menu(my_menu);
        wrefresh(menu_win);
      } else if (strcmp(choice, "Quit") == 0) {
        goto exit_app;
      }
      break;
    }
    default:
      menu_driver(my_menu, c);
      break;
    }
    wrefresh(menu_win);
  }
exit_app:
  unpost_menu(my_menu);
  free_menu(my_menu);
  for (i = 0; i < n_choices; ++i)
    free_item(items[i]);
  free(items);
  delwin(menu_win);
  endwin();
  return 0;
}
