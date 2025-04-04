#!/bin/bash
# A comprehensive setup script to install dependencies, build, and optionally install the project.
# This version offers an option to compile statically to improve portability across Linux systems.

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check for required commands
if ! command_exists gcc; then
    echo "Error: gcc is not installed. Please install gcc."
    exit 1
fi

# Check for ncurses development files (dynamic version)
if [ ! -f /usr/include/ncurses.h ] && [ ! -f /usr/include/ncurses/ncurses.h ]; then
    echo "ncurses development files not found."

    # Try to install based on available package manager
    if command_exists apt-get; then
        echo "Attempting to install libncurses-dev via apt-get..."
        sudo apt-get update && sudo apt-get install -y libncurses-dev
    elif command_exists dnf; then
        echo "Attempting to install ncurses-devel via dnf..."
        sudo dnf install -y ncurses-devel
    elif command_exists yum; then
        echo "Attempting to install ncurses-devel via yum..."
        sudo yum install -y ncurses-devel
    elif command_exists pacman; then
        echo "Attempting to install ncurses via pacman..."
        sudo pacman -S --noconfirm ncurses
    elif command_exists brew; then
        echo "Attempting to install ncurses via brew..."
        brew install ncurses
    else
        echo "No supported package manager found. Please install the ncurses development library manually."
        exit 1
    fi

    # Re-check for ncurses after installation attempt
    if [ ! -f /usr/include/ncurses.h ] && [ ! -f /usr/include/ncurses/ncurses.h ]; then
        echo "Error: ncurses development files are still missing after attempted installation."
        exit 1
    fi
fi

# Option for static linking to improve portability
read -p "Do you want to compile with static linking? (Requires static libraries, may not work with glibc fully) [y/N] " static_choice
if [[ "$static_choice" =~ ^[Yy]$ ]]; then
    # Check for the static ncurses library; path may vary by distribution.
    if [ -f /usr/lib/libncurses.a ] || [ -f /usr/lib/x86_64-linux-gnu/libncurses.a ]; then
        STATIC_FLAG="-static"
        echo "Static libraries found. Compiling statically..."
    else
        echo "Static library for ncurses not found. Falling back to dynamic linking."
        STATIC_FLAG=""
    fi
else
    STATIC_FLAG=""
fi

# Compile hotspot.c to produce hsc
echo "Compiling hotspot.c to create hsc..."
if ! gcc $STATIC_FLAG -o hsc hotspot.c -lncurses; then
    echo "Error: Compilation of hotspot.c failed."
    exit 1
fi

# Optionally compile ui.c if it exists to produce uic
if [ -f ui.c ]; then
    echo "Compiling ui.c to create uic..."
    if ! gcc $STATIC_FLAG -o uic ui.c -lncurses; then
        echo "Error: Compilation of ui.c failed."
        exit 1
    fi
fi

echo "Build successful."

# Optional installation step
read -p "Do you want to install the executables to /usr/local/bin? [y/N] " install_choice
if [[ "$install_choice" =~ ^[Yy]$ ]]; then
    echo "Installing executables..."
    sudo cp hsc /usr/local/bin/ || { echo "Error: Failed to install hsc."; exit 1; }
    if [ -f uic ]; then
        sudo cp uic /usr/local/bin/ || { echo "Error: Failed to install uic."; exit 1; }
    fi
    echo "Installation successful."
else
    echo "Installation skipped."
fi
