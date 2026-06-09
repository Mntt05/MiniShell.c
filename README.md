# 🐚 MiniShell

A minimalist shell implemented in C that replicates the core functionality of a UNIX command interpreter. Developed as an operating systems course project.

---

## Features

- **Custom prompt** in `user@host:directory$` format
- **External command execution** via `execvp` with `PATH` lookup
- **Pipelines** chaining N commands with `|`
- **I/O redirection**: stdin (`<`), stdout (`>`), and stderr (`2>`)
- **Background processes** with `&`
- **Job management**: list and bring jobs to foreground
- **Built-in commands**: `cd`, `exit`, `jobs`, `fg`
- **Signal handling**: ignores `SIGINT` and `SIGQUIT` in the main shell; child processes restore them

---

## Built-in Commands

| Command | Description |
|---------|-------------|
| `cd [path]` | Changes the working directory. Without arguments, goes to `$HOME` |
| `exit` | Terminates the shell |
| `jobs` | Lists active background processes |
| `fg [N]` | Brings job N to the foreground (without a number, brings the last one) |

---

## Build

The project depends on `parser.h` and its associated implementation. Compile with:

```bash
gcc -o minishell MiniShell.c parser.c
```

> Make sure `parser.h` and `parser.c` are in the same directory.

---

## Usage

```bash
./minishell
```

### Examples

```bash
# Simple command
user@host:~$ ls -la

# Input and output redirection
user@host:~$ sort < list.txt > sorted_list.txt

# Two-command pipeline
user@host:~$ cat file.txt | grep "error"

# Chained pipeline
user@host:~$ ps aux | grep python | wc -l

# Background process
user@host:~$ sleep 10 &
[1] 1234

# List active jobs
user@host:~$ jobs
[1] 1234 Running sleep 10

# Bring job to foreground
user@host:~$ fg 1

# Change directory
user@host:~$ cd /tmp
/tmp
```

---

## Architecture

```
MiniShell.c
├── main()                         # Main loop: prompt → read → execute
├── Prompt
│   ├── getNombre()                # Current user (getpwuid)
│   ├── getHostname()              # Machine hostname
│   └── get_cwd()                  # Current working directory
├── Job management
│   ├── add_job()                  # Adds a process to the job table
│   ├── remove_job_index()         # Removes a job by index
│   ├── find_job_by_pid()          # Looks up a job by PID
│   └── limpiar_jobs_terminados()  # Reaps zombie processes (WNOHANG)
├── Built-in commands
│   ├── comando_cd()               # cd implementation
│   ├── fg_job()                   # fg implementation
│   └── parse_fg_arg()             # Parses fg argument
├── Execution
│   ├── ejecutar_caso_simple()     # Single command with optional background
│   ├── ejecutar_pipeline()        # General N-command pipeline
│   └── procesar_linea()           # Dispatch: parses and picks execution branch
└── Redirection
    ├── redir_in()                 # stdin redirection
    ├── redir_out()                # stdout redirection
    └── redir_err()                # stderr redirection
```

---

## Dependencies

- `parser.h` / `parser.c` — command-line tokenizer (provided as part of the assignment)
- Standard POSIX libraries: `unistd.h`, `sys/wait.h`, `fcntl.h`, `signal.h`, `pwd.h`

---

## Known Limitations

- Background pipeline jobs only register the PID of the last process in the chain.
- No support for append redirection (`>>`) or heredoc (`<<`).
- No full job control: `bg` command and `SIGTSTP` signal are not implemented.
- Maximum number of simultaneous jobs is `128` (`MAX_JOBS`).
