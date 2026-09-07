# Abyss Shell

> A small Unix shell built to make process control visible.

Abyss Shell is a Linux command interpreter written in C. It is intentionally close to the operating system: commands become processes through `fork()` and `execvp()`, pipelines are connected with kernel pipes, redirection is implemented with file descriptors, and background work is tracked with nonblocking `waitpid()` calls.

The project is both a usable shell and an operating-systems study. Its control flow keeps the relationship between parsing, process creation, signals, file descriptors, and cleanup visible.

## What It Supports

### Command execution

- External commands resolved through `execvp()`.
- Parent-side built-ins: `cd`, `exit`, `jobs`, `fg`, `export`, and `env`.
- GNU Readline editing and command history.
- Output redirection with `>` and `>>`.
- Bounded argument and pipeline parsing with clear syntax errors.

### Process composition

- Multi-stage foreground pipelines such as `cat file.txt | sort | uniq`.
- Per-stage input/output wiring through `pipe()` and `dup2()`.
- Explicit descriptor closure in both parent and child processes to avoid pipeline deadlocks.

### Job control basics

- Launch a single external command in the background with `&`.
- Inspect active jobs with `jobs`.
- Bring a tracked job into the foreground with `fg <job-id>`.
- Reap completed background children without blocking the prompt.
- Preserve the parent shell on `Ctrl+C`; foreground children receive the default interrupt behavior.

## A Command's Journey

```text
Readline
	|
	v
Normalize input and detect '&'
	|
	v
Split into pipeline stages
	|
	+--> Built-in? --------------> Execute in the shell process
	|
	+--> External command --------> fork()
												  |
												  +--> child: reset signals, wire fds, execvp()
												  |
												  +--> parent: wait, or register background job
```

For a pipeline, the parent creates all required pipes before forking. Each child connects its standard input and output to the appropriate pipe, closes every inherited pipe descriptor, and then executes its command. The parent closes its copies and waits for the exact child PIDs it created.

## Architecture

| Layer | Responsibility | Representative APIs |
| --- | --- | --- |
| Input | Interactive editing, history, and EOF handling | `readline()`, `add_history()` |
| Parsing | Normalize background markers and split commands | `strtok()`, bounded arrays |
| Built-ins | Mutate shell state without forking | `chdir()`, `setenv()` |
| Execution | Create and replace processes | `fork()`, `execvp()` |
| IPC | Connect pipeline stages and redirect output | `pipe()`, `dup2()`, `open()` |
| Job tracking | Register, list, foreground, and reap jobs | `waitpid()`, `WNOHANG` |
| Signals | Keep `Ctrl+C` scoped to foreground work | `sigaction()` |

Every major feature maps to a small set of POSIX primitives rather than being hidden behind a framework.

## Quick Start

### Dependencies

- Linux or another POSIX-like environment
- GCC or a compatible C compiler
- GNU Make
- GNU Readline development headers and library

On Debian or Ubuntu:

```bash
sudo apt install build-essential libreadline-dev
```

### Build and run

```bash
git clone https://github.com/yansh07/abyss-shell.git
cd abyss-shell
make
make run
```

The executable is written to `bin/abyss-shell`.

## Examples

```text
abyss-shell> pwd
/home/user/chillc

abyss-shell> ls -la | grep '\.c$' | sort

abyss-shell> printf 'first\nsecond\n' > output.txt
abyss-shell> printf 'third\n' >> output.txt

abyss-shell> sleep 10 &
[1] 24831
abyss-shell> jobs
[1]+ Running sleep 10
abyss-shell> fg 1
sleep 10

abyss-shell> export APP_MODE=development
abyss-shell> env
...
abyss-shell> exit
```

## Scope and Current Limits

Abyss Shell focuses on process and descriptor fundamentals rather than replicating every POSIX shell feature:

- Background pipelines are rejected; only single external commands can be launched with `&`.
- There is no quoting or escaping grammar, command substitution, glob expansion, or redirection of standard input/stderr.
- Job control does not yet include process groups, terminal ownership, `stop`, or `bg`.
- The job table and stored command descriptions have fixed maximum sizes.

These boundaries keep the implementation understandable and make future extensions explicit rather than accidental.

## Development Commands

```bash
make          # Build bin/abyss-shell
make run      # Build and launch the shell
make clean    # Remove bin/ and obj/
make help     # Show available targets
```

The default build uses strict diagnostics: `-Wall -Wextra -Werror -O2 -g`.

For memory checks:

```bash
make clean all
valgrind --leak-check=full --show-leak-kinds=all ./bin/abyss-shell
```

## Project Layout

```text
.
├── Makefile              # Build, run, clean, and help targets
├── README.md             # Project design and usage
├── src/
│   └── abyss-shell.c     # Shell loop, parser, execution, IPC, signals, jobs
└── sandbox/              # Small experiments for Unix primitives
```

The `sandbox/` programs explore forks, pipes, tokenization, parsing, and Unix I/O. They are separate from the production shell path.

## Why This Project Exists

Abyss Shell is an exercise in making abstractions earn their place. It starts with kernel-facing questions:

1. Who owns the terminal when `Ctrl+C` is pressed?
2. Which process should wait, and for which PID?
3. Which file descriptors must remain open for a pipeline to make progress?
4. How can a background child be reaped without blocking interactive input?
5. Which allocations belong to the shell, and where is each one released?

The result is a compact codebase that turns those questions into observable behavior.

## License

MIT License — See LICENSE file for details.
