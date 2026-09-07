# Abyss Shell

A custom, fully functional Unix shell built from scratch in bare-metal C.

This project was developed to gain a deep, unabstracted understanding of operating system mechanics, process management, and kernel-level inter-process communication (IPC). It directly interacts with Linux system calls to manage memory, execute programs, and route data streams.

## Features

- **Process Creation & Control:** Utilizes `fork()`, `execvp()`, and `wait()` to spawn and manage child processes safely within the parent shell environment.
- **Pipeline Support:** Handles complex multi-stage pipelines (e.g., `ls -la | grep .c | wc -l`) with dynamic pipe allocation, efficient file descriptor routing, and proper cleanup to prevent deadlocks.
- **Two-Phase Parsing:** Implements a strict parsing algorithm to handle both pipe operators and command arguments without `strtok` state corruption.
- **Built-in Commands:** Supports `cd` and `exit` commands executed directly in the parent process.
- **Background Jobs:** Supports single-command jobs with `&`, plus the `jobs` command for listing running jobs.
- **Error Handling:** Comprehensive error checking and reporting for system calls.

## Technical Details

| Component | Details |
|-----------|---------|
| **Language** | C (C99 standard) |
| **Build System** | GNU Make |
| **Environment** | Linux (kernel 4.4+) |
| **Key APIs** | `fork()`, `execvp()`, `pipe()`, `dup2()`, `wait()`, `chdir()` |
| **Compiler** | GCC with strict warnings enabled |

## Installation & Build

Ensure you have `gcc` and `make` installed:

```bash
# Clone the repository
git clone https://github.com/yansh07/abyss-shell.git
cd abyss-shell

# Build the shell
make

# Run the shell
make run
```

## Usage

Once built, run the shell:

```bash
./bin/abyss-shell
```

Then use standard Unix commands:

```bash
abyss-shell> ls -l | grep .c
abyss-shell> cat file.txt | sort | uniq
abyss-shell> pwd
abyss-shell> cd /path/to/dir
abyss-shell> sleep 10 &
abyss-shell> jobs
abyss-shell> exit
```

Background pipelines are not currently supported.

## Build Targets

- `make` — Compile the executable
- `make run` — Build and run the shell
- `make clean` — Remove build artifacts
- `make help` — Show available targets

## License

MIT License — See LICENSE file for details.
