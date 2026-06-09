# 🐚 MiniShell

Una shell minimalista implementada en C que replica las funcionalidades básicas de un intérprete de comandos UNIX. Desarrollada como proyecto de sistemas operativos.

---

## Características

- **Prompt personalizado** con formato `usuario@host:directorio$`
- **Ejecución de comandos externos** mediante `execvp` con búsqueda en `PATH`
- **Pipelines** de N comandos encadenados con `|`
- **Redirección de E/S**: entrada (`<`), salida (`>`) y error estándar (`2>`)
- **Procesos en segundo plano** con `&`
- **Gestión de jobs**: listar, traer a foreground
- **Comandos internos**: `cd`, `exit`, `jobs`, `fg`
- **Manejo de señales**: ignora `SIGINT` y `SIGQUIT` en la shell principal; los hijos las restauran

---

## Comandos internos

| Comando | Descripción |
|---------|-------------|
| `cd [ruta]` | Cambia el directorio de trabajo. Sin argumentos va a `$HOME` |
| `exit` | Termina la minishell |
| `jobs` | Lista los procesos en segundo plano activos |
| `fg [N]` | Trae el job N a foreground (sin número, trae el último) |

---

## Compilación

El proyecto depende de `parser.h` y su implementación asociada. Compila con:

```bash
gcc -o minishell MiniShell.c parser.c
```

> Asegúrate de tener `parser.h` y `parser.c` en el mismo directorio.

---

## Uso

```bash
./minishell
```

### Ejemplos

```bash
# Comando simple
usuario@host:~$ ls -la

# Redirección de entrada y salida
usuario@host:~$ sort < lista.txt > lista_ordenada.txt

# Pipeline de dos comandos
usuario@host:~$ cat archivo.txt | grep "error"

# Pipeline encadenado
usuario@host:~$ ps aux | grep python | wc -l

# Proceso en background
usuario@host:~$ sleep 10 &
[1] 1234

# Ver jobs activos
usuario@host:~$ jobs
[1] 1234 Running sleep 10

# Traer job a foreground
usuario@host:~$ fg 1

# Cambiar de directorio
usuario@host:~$ cd /tmp
/tmp
```

---

## Arquitectura

```
MiniShell.c
├── main()                         # Bucle principal: prompt → lectura → ejecución
├── Prompt
│   ├── getNombre()                # Usuario actual (getpwuid)
│   ├── getHostname()              # Nombre del host
│   └── get_cwd()                  # Directorio de trabajo actual
├── Gestión de jobs
│   ├── add_job()                  # Añade un proceso a la tabla de jobs
│   ├── remove_job_index()         # Elimina un job por índice
│   ├── find_job_by_pid()          # Busca un job por PID
│   └── limpiar_jobs_terminados()  # Recoge procesos zombie (WNOHANG)
├── Comandos internos
│   ├── comando_cd()               # Implementación de cd
│   ├── fg_job()                   # Implementación de fg
│   └── parse_fg_arg()             # Parser del argumento de fg
├── Ejecución
│   ├── ejecutar_caso_simple()     # Un solo comando con posible background
│   ├── ejecutar_pipeline()        # Pipeline general de N comandos
│   └── procesar_linea()           # Dispatch: parsea y elige rama de ejecución
└── Redirección
    ├── redir_in()                 # Redirección de stdin
    ├── redir_out()                # Redirección de stdout
    └── redir_err()                # Redirección de stderr
```

---

## Dependencias

- `parser.h` / `parser.c` — tokenizador de líneas de comandos (proporcionado por la práctica)
- Bibliotecas estándar POSIX: `unistd.h`, `sys/wait.h`, `fcntl.h`, `signal.h`, `pwd.h`

---

## Limitaciones conocidas

- Los jobs en background en un pipeline solo registran el PID del último proceso.
- No hay soporte para redirección con `>>` (append) ni heredoc (`<<`).
- No implementa control de trabajos completo (no hay `bg`, ni señal `SIGTSTP`).
- El número máximo de jobs simultáneos es `128` (`MAX_JOBS`).
