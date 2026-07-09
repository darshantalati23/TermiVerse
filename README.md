# TermiVerse

## Table of Contents
1. [System Demo](#system-demo-except-chat)
2. [Chat Demo](#chat-demo)
3. [Architecture](#architecture)
4. [About the Creators](#about-the-creators)
5. [Tech Stack](#tech-stack)
6. [Build & Run](#build--run)
7. [Features & Commands](#features--commands)
8. [Performance & Benchmarking](#performance--benchmarking)

## System Demo (except chat)

![TermiVerse Single-Pane Demo](assets/termiverse_demo.gif)

## Chat Demo

![TermiVerse Chat Demo](assets/chat_demo.gif)

## Architecture

TermiVerse is a shell-like environment where a central **Launcher** manages multiple
application processes. The launcher (`launcher/launcher.c`) reads user commands,
forks child processes from the `bin/` directory, and handles foreground/background
job control via POSIX signals. Each application is a standalone binary.

| Module | Description |
|---|---|
| **Launcher** | Custom shell with job control (fg/bg/jobs), POSIX signal handling, and a command parser supporting quoted arguments and background execution |
| **Chat System** | Multi-client chat over Named Pipes (FIFOs). `chat_server` runs one thread per connected client; `chat_client` spawns a sender thread for concurrent input/output |
| **Alarm** | Countdown timer that writes a message file and sends SIGUSR1 to the launcher process when time expires |
| **Calculator** | Evaluates a simple arithmetic expression passed as arguments and prints the result |
| **Notes** | Persistent plaintext note storage backed by `termiverse_notes.txt`, with flock-based file locking for safe concurrent access |
| **Tetris** | Terminal Tetris with ghost piece, line scoring, and audio via libao |
| **Snake** | Terminal Snake rendered with ncurses |
| **TurtleSim** | Interactive trading simulator with a custom 2D console chart rendering, support for real-world historical data (Yahoo Finance) and synthetic Brownian motion |

## About the creators

This project was made collectively by the students of Dhirubhai Ambani University (formerly DA-IICT):

1. Darshan Talati (Student ID: _202401046_)
2. Dharmesh Upadhyay (Student ID: _202401049_)
3. Aarohi Mehta (Student ID: _202401002_)
4. Alvita Thakor (Student ID: _202401012_)

## Tech Stack

| Technology | Usage |
|---|---|
| C | Launcher, Chat Server/Client, Alarm, Calculator, Notes |
| C++ | Tetris, Snake |
| POSIX Threads (pthreads) | Per-client threads in chat server; sender thread in chat client |
| Named Pipes (FIFOs) | IPC channel between chat server and each client |
| Unix Signals | Job control (SIGCHLD, SIGTSTP), alarm notification (SIGUSR1) |
| flock | File locking in the Notes app |
| termios | Raw terminal mode for Tetris and Snake |
| Makefile | Per-module and root-level build system |

## Build & Run

```sh
make clean && make
./bin/launcher
```

**Windows users:** TermiVerse requires a Linux environment and will not run natively
on Windows. Use WSL2 (Windows Subsystem for Linux 2) — open a WSL2 terminal, navigate
to the project directory (e.g. `cd /mnt/c/Darshan/TermiVerse`), and run the commands
above. WSL2 provides a full Linux kernel, so no code changes are necessary.

## Features / Commands

### Applications

| Command | Description |
|---|---|
| `chat <name>` | Join the global chat room |
| `calc <num> <op> <num>` | Calculator (e.g. `calc 10 + 5`) |
| `launch alarm <sec> <msg>` | Set a countdown timer (e.g. `launch alarm 5 "Run"`) |
| `launch turtlesim live` | Play the live trading simulation game against a Bot (using synthetic Brownian motion) |
| `launch turtlesim backtest <Asset>` | Play the interactive trading game on historical real-world data (e.g., `GOLD`, `AAPL`, `TSLA` from Yahoo Finance) |
| `launch notes <cmd>` | Notes app — subcommands: `add "text"`, `read`, `clear` |
| `launch snake` | Snake game |
| `launch tetris` | Tetris game |

### System Commands

| Command | Description |
|---|---|
| `jobs` | List all background jobs |
| `fg %<id>` | Resume a stopped or background job in the foreground |
| `bg %<id>` | Resume a stopped job in the background |
| `help` | Show the application menu |
| `quit` | Shutdown TermiVerse |

### Chat Commands

While connected via `chat <name>`:

| Command | Description |
|---|---|
| `/dm <user> <msg>` | Send a direct message to a connected user |
| `/list` | Show all currently connected users |
| `/history` | Display the last 20 public messages |
| `/quit` | Disconnect and exit the chat |

## Performance & Benchmarking

TermiVerse includes a dedicated concurrency stress-test tool (`bin/ipc_bench`) to evaluate the throughput of the POSIX Named Pipe architecture.

| Command | Description |
|---|---|
| `./bin/ipc_bench <clients> <seconds>` | Benchmarks the chat server using N parallel threads (e.g. `./bin/ipc_bench 10 5`) |

### Stress-Test Metrics
Under test loads in a standard Linux environment (10 concurrent clients blasting messages continuously for 5 seconds):
- **Total Blaster Throughput**: **~890,000 messages/sec** routed and broadcasted.
- **IPC Safety**: Zero deadlocks, zero lock contention freezes, and 100% thread safety validated under maximum queue backpressure.