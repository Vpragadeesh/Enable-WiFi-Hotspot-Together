# Hotspot UI Project

This project implements basic hotspot functionality along with a user interface. It contains source files for both components and a setup script to help configure and build the project environment.

## File Structure

- **hotspot.c** – Source code for the hotspot (network or connectivity) functionality.
- **ui.c** – Source code for the user interface component.
- **setup.sh** – A comprehensive shell script to set up, build, and optionally install the project.
- **hsc** – The compiled binary for the hotspot module.
- **uic** – The compiled binary for the UI module (if available).

## Requirements

- A C compiler (e.g., GCC or Clang)
- A Unix-like environment (Linux, macOS)
- A supported package manager (e.g., apt-get, dnf, yum, pacman, or Homebrew) to install the ncurses development library

## Setup and Installation

### 1. Clone the Repository

Clone the repository to your local machine:

```bash
git clone https://github.com/Vpragadeesh/Enable-WiFi-Hotspot-Together
cd Enable-WiFi-Hotspot-Together
```

### 2. Next convert setup.sh to executable

```
bash
chmod +x setup.sh

./setup.sh
```

This README now includes a detailed description of the setup script's features, along with clear instructions for making it executable and running it.
