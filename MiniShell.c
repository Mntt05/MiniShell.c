#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include <unistd.h>
#include <pwd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>

// Definimos el número máximo de jobs que vamos a gestionar simultáneamente
#define MAX_JOBS 128

// Estructura para representar un proceso en segundo plano (job)
typedef struct {
    pid_t pid;              
    char  cmdline[1024];   
    int   active;           
} job_t;

// Declaramos la tabla de jobs y un contador de jobs activos
job_t jobs[MAX_JOBS];
int   num_jobs = 0;

// Obtenemos el nombre de usuario usando el UID efectivo del proceso
const char *getNombre() {
    uid_t uid = geteuid();
    struct passwd *pw = getpwuid(uid);
    return pw ? pw->pw_name : "unknown";
}

// Obtenemos el nombre del host de la máquina donde se ejecuta la minishell
const char *getHostname() {
    static char hostname[256];
    return gethostname(hostname, sizeof(hostname)) == 0 ? hostname : "unknown";
}

// Obtenemos el directorio de trabajo actual para mostrarlo en el prompt
const char *get_cwd() {
    static char cwd[1024];
    return getcwd(cwd, sizeof(cwd)) ? cwd : "???";
}

// Construimos e imprimimos el prompt con el formato usuario@host:cwd$
void print_prompt() {
    printf("%s@%s:%s$ ", getNombre(), getHostname(), get_cwd());
    fflush(stdout);
}

// Añadimos un nuevo job a la tabla de procesos en background
int add_job(pid_t pid, const char *line) {
    if (num_jobs >= MAX_JOBS) return -1;
    jobs[num_jobs].pid = pid;
    strncpy(jobs[num_jobs].cmdline, line, sizeof(jobs[num_jobs].cmdline) - 1);
    jobs[num_jobs].cmdline[sizeof(jobs[num_jobs].cmdline) - 1] = '\0';
    jobs[num_jobs].active = 1;
    return num_jobs++;
}

// Eliminamos un job por índice y compactamos la tabla desplazando el resto hacia delante
void remove_job_index(int idx) {
    if (idx < 0 || idx >= num_jobs) return;
    for (int i = idx; i < num_jobs - 1; i++) jobs[i] = jobs[i + 1];
    num_jobs--;
}

// Buscamos el índice de un job a partir de su PID para poder actualizarlo o eliminarlo
int find_job_by_pid(pid_t pid) {
    for (int i = 0; i < num_jobs; i++)
        if (jobs[i].pid == pid && jobs[i].active) return i;
    return -1;
}

// Recorremos los procesos hijos terminados sin bloquear y limpiamos la tabla de jobs
void limpiar_jobs_terminados() {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int idx = find_job_by_pid(pid);
        if (idx != -1) remove_job_index(idx);
    }
}

// Analizamos si la línea de entrada es un comando 'fg' con o sin número
// Devolvemos -1 si no es fg, 0 si es 'fg' sin argumentos, o N si es 'fg N'
int parse_fg_arg(char *line) {
    while (*line == ' ' || *line == '\t') line++;
    if (strncmp(line, "fg", 2) != 0) return -1;
    line += 2;
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0') return 0;
    int id = atoi(line);
    return id <= 0 ? -1 : id;
}

// Implementamos el comando interno 'fg' para traer un job a foreground
void fg_job(int id) {
    if (num_jobs == 0) {
        printf("fg: no hay trabajos\n");
        return;
    }

    // Decidimos qué job recuperar: el último o el que nos indiquen
    int idx = (id == 0) ? num_jobs - 1 : id - 1;

    if (id != 0 && (id < 1 || id > num_jobs)) {
        printf("fg: trabajo %d no existe\n", id);
        return;
    }
    if (!jobs[idx].active) {
        printf("fg: trabajo %d no activo\n", id == 0 ? idx + 1 : id);
        return;
    }

    pid_t pid = jobs[idx].pid;

    // Mostramos el comando asociado al job para que el usuario sepa qué se está trayendo
    printf("%s\n", jobs[idx].cmdline);

    // Esperamos a que el proceso termine en foreground
    int status;
    waitpid(pid, &status, 0);

    // Eliminamos el job de la lista porque ya ha finalizado
    remove_job_index(idx);
}

// Gestionamos la redirección de entrada duplicando el descriptor sobre STDIN
void redir_in(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); exit(EXIT_FAILURE); }
    if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2 input"); close(fd); exit(EXIT_FAILURE); }
    close(fd);
}

// Gestionamos la redirección de salida duplicando el descriptor sobre STDOUT
void redir_out(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { perror(path); exit(EXIT_FAILURE); }
    if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2 output"); close(fd); exit(EXIT_FAILURE); }
    close(fd);
}

// Gestionamos la redirección de error duplicando el descriptor sobre STDERR
void redir_err(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { perror(path); exit(EXIT_FAILURE); }
    if (dup2(fd, STDERR_FILENO) < 0) { perror("dup2 error"); close(fd); exit(EXIT_FAILURE); }
    close(fd);
}

// Ejecutamos una línea con un solo comando sin pipes, con posibles redirecciones y background
void ejecutar_caso_simple(tline *l, const char *line) {
    if (l->ncommands != 1) return;

    tcommand *cmd = &l->commands[0];
    if (!cmd->argv || !cmd->argv[0]) return;

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return; }

    if (pid == 0) {
        // En el hijo restauramos el comportamiento por defecto de las señales de teclado
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);

        // Aplicamos las redirecciones que haya indicado el parser
        if (l->redirect_input)  redir_in(l->redirect_input);
        if (l->redirect_output) redir_out(l->redirect_output);
        if (l->redirect_error)  redir_err(l->redirect_error);

        // Ejecutamos el comando usando la búsqueda en PATH
        execvp(cmd->argv[0], cmd->argv);

        // Si exec falla, mostramos el mensaje requerido por la práctica
        fprintf(stderr, "%s: No se encuentra el mandato\n", cmd->argv[0]);
        exit(EXIT_FAILURE);
    } else {
        // En el padre decidimos si es foreground o background
        if (l->background) {
            int idx = add_job(pid, line);
            printf("[%d] %d\n", idx + 1, pid);
        } else {
            int status;
            waitpid(pid, &status, 0);
        }
    }
}

// Ejecutamos un pipeline de exactamente dos comandos
void ejecutar_dos_comandos_con_pipe(tline *l) {
    if (l->ncommands != 2 || l->background != 0) return;

    int pipefd[2];
    if (pipe(pipefd) < 0) { perror("pipe"); return; }

    tcommand *cmd1 = &l->commands[0];
    tcommand *cmd2 = &l->commands[1];
    if (!cmd1->argv || !cmd1->argv[0] || !cmd2->argv || !cmd2->argv[0]) {
        close(pipefd[0]); close(pipefd[1]); return;
    }

    // Primer hijo: escribe en el pipe
    pid_t pid1 = fork();
    if (pid1 < 0) { perror("fork"); close(pipefd[0]); close(pipefd[1]); return; }

    if (pid1 == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        if (l->redirect_input) redir_in(l->redirect_input);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) { perror("dup2 pipe cmd1"); exit(EXIT_FAILURE); }
        close(pipefd[0]); close(pipefd[1]);
        execvp(cmd1->argv[0], cmd1->argv);
        fprintf(stderr, "%s: No se encuentra el mandato\n", cmd1->argv[0]);
        exit(EXIT_FAILURE);
    }

    // Segundo hijo: lee del pipe
    pid_t pid2 = fork();
    if (pid2 < 0) { perror("fork"); close(pipefd[0]); close(pipefd[1]); return; }

    if (pid2 == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        if (dup2(pipefd[0], STDIN_FILENO) < 0) { perror("dup2 pipe cmd2"); exit(EXIT_FAILURE); }
        if (l->redirect_output) redir_out(l->redirect_output);
        close(pipefd[0]); close(pipefd[1]);
        execvp(cmd2->argv[0], cmd2->argv);
        fprintf(stderr, "%s: No se encuentra el mandato\n", cmd2->argv[0]);
        exit(EXIT_FAILURE);
    }

    // El padre cierra los extremos del pipe y espera a ambos hijos
    close(pipefd[0]); close(pipefd[1]);
    int status;
    waitpid(pid1, &status, 0);
    waitpid(pid2, &status, 0);
}

// Ejecutamos un pipeline general con N comandos con posibilidad de background
void ejecutar_pipeline(tline *l) {
    int n = l->ncommands;
    int pipes[n - 1][2];
    pid_t last_pid = -1;

    if (n < 2) return;

    // Creamos todos los pipes que necesitamos (uno entre cada par de comandos)
    for (int i = 0; i < n - 1; i++)
        if (pipe(pipes[i]) < 0) { perror("pipe"); return; }

    // Creamos un hijo por cada comando del pipeline
    for (int i = 0; i < n; i++) {
        tcommand *cmd = &l->commands[i];
        if (!cmd->argv || !cmd->argv[0]) continue;

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); continue; }
        if (i == n - 1) last_pid = pid;  // Guardamos el PID del último para jobs en background

        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);

            // Conectamos la entrada al pipe anterior, si no es el primero
            if (i > 0 && dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                perror("dup2 stdin pipe"); exit(EXIT_FAILURE);
            }
            // Conectamos la salida al pipe siguiente, si no es el último
            if (i < n - 1 && dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                perror("dup2 stdout pipe"); exit(EXIT_FAILURE);
            }

            // Aplicamos redirecciones sólo donde corresponde según el enunciado
            if (i == 0 && l->redirect_input)      redir_in(l->redirect_input);
            if (i == n - 1 && l->redirect_output) redir_out(l->redirect_output);
            if (i == n - 1 && l->redirect_error)  redir_err(l->redirect_error);

            // Cerramos todos los extremos de pipe que ya no necesitamos en el hijo
            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // Ejecutamos el comando del pipeline
            execvp(cmd->argv[0], cmd->argv);
            fprintf(stderr, "%s: No se encuentra el mandato\n", cmd->argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    // En el padre cerramos todos los pipes
    for (int i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Si la línea es en background, registramos el job y devolvemos el prompt
    if (l->background) {
        int idx = add_job(last_pid, l->commands[0].argv[0]);
        printf("[%d] %d\n", idx + 1, last_pid);
    } else {
        // En foreground esperamos a que terminen todos los procesos del pipeline
        int status;
        while (wait(&status) > 0) ;
    }
}

// Procesamos una línea ya leída: la parseamos y decidimos qué rama ejecutar
void procesar_linea(char *line) {
    tline *l = tokenize(line);
    if (!l || l->ncommands == 0) return;

    // Caso de un único mandato
    if (l->ncommands == 1) {
        ejecutar_caso_simple(l, line);
        return;
    }

    // Caso de dos o más mandatos (pipeline)
    if (l->ncommands >= 2) {
        ejecutar_pipeline(l);
        return;
    }
}

// Implementamos el comando interno 'cd' (sin pipes), según la práctica
int comando_cd(char *line) {
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "cd", 2) != 0) return 0;
    p += 2;
    while (*p == ' ' || *p == '\t') p++;

    const char *ruta;

    // Si no se especifica ruta, usamos el directorio HOME
    if (*p == '\0') {
        ruta = getenv("HOME");
        if (!ruta) { fprintf(stderr, "cd: HOME no está definido\n"); return 1; }
    } else ruta = p;

    // Intentamos cambiar de directorio y mostramos un error si falla
    if (chdir(ruta) < 0) { perror("cd"); return 1; }

    // Mostramos la ruta absoluta del nuevo directorio de trabajo
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) printf("%s\n", cwd);
    return 1;
}

// Bucle principal de la minishell: mostramos el prompt, leemos y ejecutamos líneas
int main() {
    char line[1024];

    // Ignoramos SIGINT y SIGQUIT en la minishell; los hijos los restauran
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    while (1) {
        print_prompt();

        // Leemos una línea; si recibimos EOF (Ctrl+D), salimos
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\nSaliendo de la minishell...\n");
            break;
        }

        // Eliminamos el salto de línea final
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '\0') continue;

        // Comando interno 'exit' para terminar la minishell
        if (strcmp(line, "exit") == 0) {
            printf("Saliendo de la minishell...\n");
            break;
        }

        // Comando interno 'cd'
        if (comando_cd(line)) continue;

        // Comando interno 'jobs'
        if (strcmp(line, "jobs") == 0) {
            limpiar_jobs_terminados();
            for (int i = 0; i < num_jobs; i++)
                if (jobs[i].active)
                    printf("[%d] %d Running %s\n", i + 1, jobs[i].pid, jobs[i].cmdline);
            continue;
        }

        // Comando interno 'fg' (con o sin número)
        int fg_id = parse_fg_arg(line);
        if (fg_id >= 0) { fg_job(fg_id); continue; }

        // Para cualquier otra línea usamos el parser y las funciones de ejecución
        procesar_linea(line);
    }
}

