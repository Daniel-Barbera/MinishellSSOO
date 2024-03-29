#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h> // Signal handling
#include <unistd.h> // getcwd(), gethostname()
#include <limits.h> // PATH_MAX, HOST_NAME_MAX, FILENAME_MAX
#include <stdlib.h> // exit(), getenv(), chdir(), etc.
#include <sys/wait.h> // waitpid()
#include <sys/types.h> // pid_t
#include <sys/stat.h> 
#include <errno.h> // errno
#include <locale.h> // setlocale()
#include "parser.h"
/** Constant definitions */
#define BACKGROUND_JOBS_MAX (size_t) 10
#define MAX_COMMAND_LENGTH (size_t) 1024
#define BOLD_RED "\x1b[31;1m"
#define BOLD_GREEN "\x1b[32;1m"
#define BOLD_BLUE "\x1b[34;1m"
#define BOLD_PURPLE "\x1b[35;1m"
#define WHITE "\x1b[0m"
#define YELLOW "\x1b[33m"
#define RESET "\x1b[0m"

// Shell prompt helpers
bool prompt();
void print_prompt();
void print_color(FILE * stream, char * color, char * text);
char * polite_directory_format(char * name);
// Signal handlers
void sigint_handler();
void sigchld_handler();
void exit_handler();
// Shell helpers
void set_up_preconditions();
char * check_if_all_commands_are_valid(tline * line);
bool set_redirection_variables(tline * line);
bool set_redirection_file(char * filename, FILE ** file_ptr);
char * builtin_commands_are_present(tline * line);
void pipe_commands(tline * line);
void execute_command(tline * command);
void push_background_job_to_list(pid_t pid);
void dup2_or_exit(int old_fd, int new_fd);
void signal_or_exit(int signum, void * handler);
void close_redirection_files();
void remove_background_job(pid_t pid);
// Shell commands
void cd(char * path);
void umask_impl(tcommand command);
void foreground(size_t job_id);
void jobs();
void print_symbolic_umask(mode_t umask);

// Type definitions
/** PID and string representation of a job. */
typedef struct {
  pid_t pid;
  char * command;
} background_job;

// Global variables
                                                     // Used for error handling.
pid_t foreground_job_pid;                            // PID of the foreground job
char input_buffer[PATH_MAX];                         // Buffer to store user input. Useful as well to list background jobs.
background_job background_jobs[BACKGROUND_JOBS_MAX]; // List of background jobs
size_t background_jobs_idx;                          // Index of the next available position in background_jobs
// Variables for output redirection
FILE * input_file;                                   // File to redirect input from
FILE * output_file;                                  // File to redirect output to
FILE * error_file;                                   // File to redirect error to
int stderr_fd;                                       // File descriptor of stderr

int main() {
  char * bad_command;
  tline * line;

  set_up_preconditions();
  while (prompt()) {
    if (input_buffer[0] == '\n') {
      continue;
    }
    line = tokenize(input_buffer);
    if ((bad_command = check_if_all_commands_are_valid(line)) && bad_command) {
      fprintf(error_file, "%smsh: comando no encontrado: %s%s\n", BOLD_RED, bad_command, RESET);
      continue;
    }
    if (!set_redirection_variables(line)) {
      continue;
    }
    if (line->ncommands > 1) {
      if((bad_command = builtin_commands_are_present(line)) && bad_command) {
        fprintf(error_file, "%smsh: no es posible usar el comando %s con pipes.%s\n", BOLD_RED, bad_command, RESET);
        continue;
      }
      pipe_commands(line);
    } else {
      execute_command(line);
    }
    close_redirection_files();
  }
  return 0;
}

// Shell prompt helpers 
/** Print the prompt, and get user input.*/
bool prompt() {
  char * input;
  print_prompt();
  input = fgets(input_buffer, PATH_MAX, stdin);
  if (!input) {
    exit(EXIT_SUCCESS);
  }
  return true;
}
/** Print the msh> prompt. */
void print_prompt() {
  char prompt[PATH_MAX], 
       hostname[HOST_NAME_MAX];

  gethostname(hostname, sizeof(hostname));
  sprintf(prompt, "%s@%s", getenv("USER"), hostname);
  print_color(stdout, BOLD_GREEN, prompt);
  printf(":");
  getcwd(prompt, sizeof(prompt));
  print_color(stdout, BOLD_BLUE, polite_directory_format(prompt));
  printf(" msh> ");
}
// Print a string in a given color to a stream.
void print_color(FILE * stream, char * color, char * string) {
  fprintf(stream, "%s%s%s", color, string, RESET);
}
/** https://github.com/bminor/bash/blob/fb0092fb0e7bb3121d3b18881f72177bcb765491/general.c#L904
 *  Takes a path string, and if the environment variable HOME is part of it, replaces the HOME part with "~". */
char * polite_directory_format(char * name) {
  char * home = getenv("HOME");
  static char tdir[PATH_MAX];
  int length;

  length = home ? strlen(home) : 0;
  if (length > 1 && strncmp(home, name, length) == 0 && (!name[length] || name[length] == '/'))
    {
      strncpy(tdir + 1, name + length, sizeof(tdir) - 2);
      tdir[0] = '~';
      tdir[sizeof(tdir) - 1] = '\0';
      return (tdir);
    }
  else
    return (name);
}
// Signal handlers
/** Handles SIGINT: interrupt foreground process, and print prompt. */ 
void sigint_handler() {
  signal_or_exit(SIGINT, sigint_handler);
  printf("\n");
  if (foreground_job_pid <= 0) {
    print_prompt();
  } else {
    kill(foreground_job_pid, SIGTERM);
  }
  fflush(stdout);
}
/**  Handles SIGCHLD: remove terminated background jobs from the list,
 *   and print a message if the job was terminated by a signal. */
void sigchld_handler() {
  int status;
  pid_t dead_process_id;

  signal_or_exit(SIGCHLD, sigchld_handler);
  dead_process_id = waitpid(-1, &status, WNOHANG | WUNTRACED);
  if (dead_process_id <= 0) {
    return;
  }
  for (size_t i = 0; i < background_jobs_idx; i++) {
    if (dead_process_id == background_jobs[i].pid) {
      if (WIFEXITED(status)) {
        printf("\n[%zu] terminado con status %d: %s", i, WEXITSTATUS(status), background_jobs[i].command);
      } else if (WIFSIGNALED(status)) {
        printf("\n[%zu] terminado por la señal \"%s\": %s", i, strsignal(WTERMSIG(status)), background_jobs[i].command);
      }
      remove_background_job(dead_process_id);
      print_prompt();
      fflush(stdout);
      return;
    }
  }
}
/** Handles any termination signals by clearing any memory allocated, and politely exiting the program. */
void exit_handler() {
  close_redirection_files();
  if (foreground_job_pid > 0) {
    kill(foreground_job_pid, SIGTERM);
  }
  for (size_t i = 0; i < background_jobs_idx; i++) {
    if (background_jobs[i].pid != 0) {
      kill(background_jobs[i].pid, SIGTERM);
    }
    if (background_jobs[i].command) {
      free(background_jobs[i].command);
    }
  }
  printf("%sMemoria liberada. ¡Adiós!%s\n", BOLD_PURPLE, RESET);
  exit(EXIT_SUCCESS);
}
// Shell helpers
void set_up_preconditions() {
  input_file = stdin;
  output_file = stdout;
  error_file = stderr;
  stderr_fd = dup(STDERR_FILENO);
  setlocale(LC_ALL, "es_ES.UTF-8"); // Set locale to Spanish, else default to the system locale.
  printf("%sBienvenido a myshell (msh). Autor: Daniel Barbera (2022) bajo licencia GPL.\n%s", BOLD_GREEN, RESET);
  signal_or_exit(SIGINT, sigint_handler);
  signal_or_exit(SIGCHLD, sigchld_handler);
  signal_or_exit(SIGQUIT, exit_handler);
  signal_or_exit(SIGTERM, exit_handler);
  signal_or_exit(SIGHUP, exit_handler);
  atexit(exit_handler);
}
/** Set input, output and error redirection. 
 *  If they're not enabled, reset them to stdin, stdout and stderr. */ 
bool set_redirection_variables(tline * line) {
  input_file = stdin;
  output_file = stdout;
  error_file = stderr;
  if (
    set_redirection_file(line->redirect_input, &input_file)
    && set_redirection_file(line->redirect_output, &output_file)
    && set_redirection_file(line->redirect_error, &error_file)
  ) return true; 
  return false; 
}
/** Helper for above function. */
bool set_redirection_file(char * filename, FILE ** file_ptr) {
  FILE * aux_file_ptr;

  if (!filename) {
    return true;
  }
  if (* file_ptr == input_file) {
    aux_file_ptr = fopen(filename, "r");
  } else {
    aux_file_ptr = fopen(filename, "w");
  }
  if (!aux_file_ptr) {
    fprintf(error_file, "%s%s: %s%s\n", BOLD_RED, filename, strerror(errno), RESET);
    return false;
  }
  * file_ptr = aux_file_ptr;
  return true;
}
/** Determines if any of the commands entered is not valid. */ 
char * check_if_all_commands_are_valid(tline * line) {
  if (!line) return NULL;
  for (size_t i = 0; i < (size_t) line->ncommands; i++) {
    if (
      !line->commands[i].filename
      && strcmp(line->commands[i].argv[0], "cd") != 0
      && strcmp(line->commands[i].argv[0], "exit") != 0
      && strcmp(line->commands[i].argv[0], "jobs") != 0
      && strcmp(line->commands[i].argv[0], "fg") != 0
      && strcmp(line->commands[i].argv[0], "umask") != 0
    ) {
      return line->commands[i].argv[0];
    }
  }
  return NULL;
}
/**  Determines if the line contains the commands "cd" or "exit". */
char * builtin_commands_are_present(tline * line) {
  for (size_t i = 0; i < (size_t) line->ncommands; i++) {
    if (
      strcmp(line->commands[i].argv[0], "cd") == 0 
      || strcmp(line->commands[i].argv[0], "exit") == 0
      || strcmp(line->commands[i].argv[0], "jobs") == 0
      || strcmp(line->commands[i].argv[0], "fg") == 0
      || strcmp(line->commands[i].argv[0], "umask") == 0
    ) {
      return line->commands[i].argv[0];
    }
  }
  return NULL;
}
/** Parse a tcommand to string to display it when jobs() is called. Push it to the queue. */ 
void push_background_job_to_list(pid_t pid) {
  char * command;

  command = malloc(sizeof(char) * (strlen(input_buffer) + 1));
  strcpy(command, input_buffer);
  background_jobs[background_jobs_idx].pid = pid;
  background_jobs[background_jobs_idx].command = command;
  background_jobs_idx++;
}
/** Remove a job from the queue. */
void remove_background_job(pid_t pid) {
  for (size_t i = 0; i < background_jobs_idx; i++) {
    if (background_jobs[i].pid != pid) continue;
    background_jobs[i].pid = 0;
    if (background_jobs[i].command) {
      free(background_jobs[i].command);
    }
    for (size_t j = i; j < background_jobs_idx - 1; j++) {
      background_jobs[j] = background_jobs[j + 1];
    }
    background_jobs_idx--;
    return;
  }
}
/** Execute the commands */
void pipe_commands(tline * line) {
  int ** pipes;
  pid_t pid;

  // Pipes will be a 2D array of size ncommands - 1.
  // Each row will contain two file descriptors, one for reading and one for writing.
  pipes = malloc(sizeof(int *) * (line->ncommands - 1));
  for (size_t i = 0; i < (size_t) line->ncommands - 1; i++) {
    pipes[i] = malloc(sizeof(int) * 2);
    if (pipe(pipes[i]) == -1) {
      fprintf(error_file, "%sError creating pipe: %s%s\n", BOLD_RED, strerror(errno), RESET);
      for (size_t j = 0; j < i; j++) {
        free(pipes[j]);
      }
      free(pipes);
      return;
    }
  }
  dup2(fileno(error_file), STDERR_FILENO);
  for (size_t i = 0; i < (size_t) line->ncommands; i++) {
    pid = fork();
    if (pid == -1) {
      fprintf(error_file, "%sError forking: %s%s\n", BOLD_RED, strerror(errno), RESET);
      return;
    }
    if (pid == 0) {
      // Child process.
      if (i == 0) {
        // First command.
        dup2_or_exit(fileno(input_file), STDIN_FILENO);
      } else {
        dup2_or_exit(pipes[i - 1][0], STDIN_FILENO);
      }
      if (i == (size_t) line->ncommands - 1) {
        // Last command.
        dup2_or_exit(fileno(output_file), STDOUT_FILENO);
      } else {
        dup2_or_exit(pipes[i][1], STDOUT_FILENO);
      }
      for (size_t j = 0; j < (size_t) line->ncommands - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      execvp(line->commands[i].filename, line->commands[i].argv);
      fprintf(error_file, "%s%s: %s%s\n", BOLD_RED, line->commands[i].argv[0], strerror(errno), RESET);
      exit(EXIT_FAILURE);
    } else {
      // Parent process.
      if (i != (size_t) line->ncommands - 1) {
        if (!line->background) {
          foreground_job_pid = pid;
          waitpid(pid, NULL, 0);
          foreground_job_pid = 0;
        }
      } else {
        if (line->background) {
          push_background_job_to_list(pid);
        } else {
          foreground_job_pid = pid;
          waitpid(pid, NULL, 0);
          foreground_job_pid = 0;
        }
      }
      if (i != (size_t) line->ncommands - 1) {
        close(pipes[i][1]);
      }
    }
  }
  free(pipes);
}
/** Execute a command. */
void execute_command(tline * line) {
  size_t job_id; // Used for fg.
  pid_t pid;

  if (!line->commands[0].filename) {
    if (strcmp(line->commands[0].argv[0], "cd") == 0) {
        cd(line->commands[0].argv[1]);
        return;
    } 
    if (strcmp(line->commands[0].argv[0], "exit") == 0) {
      exit(EXIT_SUCCESS);
    } 
    if (strcmp(line->commands[0].argv[0], "umask") == 0) {
      umask_impl(line->commands[0]);
      return;
    } 
    if (strcmp(line->commands[0].argv[0], "jobs") == 0) {
      jobs();
      return;
    } 
    if (strcmp(line->commands[0].argv[0], "fg") == 0) {
      if (!line->commands[0].argv[1]) {
        foreground(0);
        return;
      }
      job_id = atoi(line->commands[0].argv[1]);
      if (job_id == 0 && line->commands[0].argv[1][0] != '0') {
        fprintf(error_file, "%sfg: %s no es un número válido.%s\n", BOLD_RED, line->commands[0].argv[1], RESET);
        return;
      }
      foreground(job_id);
      return;
    }
  } 
  pid = fork();
  if (pid == -1) {
    fprintf(error_file, "%sfork: %s%s\n", BOLD_RED, strerror(errno), RESET);
    return;
  }
  if (pid == 0) {
    // Child process
    // If the child process is a background process, it ignores the SIGINT signal.
    // The appropriate way to do this would be to create a new process group or session.
    if (line->background) {
      signal_or_exit(SIGINT, SIG_IGN);
    } else {
      signal_or_exit(SIGINT, SIG_DFL);
    }
    if (line->redirect_input) dup2_or_exit(fileno(input_file), STDIN_FILENO);
    if (line->redirect_output) dup2_or_exit(fileno(output_file), STDOUT_FILENO);
    if (line->redirect_error) dup2_or_exit(fileno(error_file), STDERR_FILENO);
    execvp(line->commands[0].filename, line->commands[0].argv);
    fprintf(
      error_file, 
      "%s%s: %s%s\n",
      BOLD_RED,
      line->commands[0].argv[0],
      strerror(errno),
      RESET
    );
    exit(EXIT_FAILURE);
  } else {
    // Parent process
    if (line->background) {
      push_background_job_to_list(pid);
      return;
    } else {
      foreground_job_pid = pid;
      waitpid(pid, NULL, 0);
      foreground_job_pid = 0;
    }
  }
} 

/** Duplicate file descriptor. Return false if cannot. */
void dup2_or_exit(int old_fd, int new_fd) {
  if (dup2(old_fd, new_fd) == -1) {
    fprintf(error_file, "%sdup2: %s%s\n", BOLD_RED, strerror(errno), RESET);
    exit(EXIT_FAILURE);
  }
}
void signal_or_exit(int signum, void * handler) {
  if (signal(signum, handler) == SIG_ERR) {
    fprintf(error_file, "%ssignal: %s%s\n", BOLD_RED, strerror(errno), RESET);
    exit(EXIT_FAILURE);
  }
}
/** Set signal handler or return false */
/** Close redirection files if they're not stdin, stdout or stderr. */
void close_redirection_files() {
  int input_fd =  fileno(input_file),
      output_fd = fileno(output_file),
      error_fd =  fileno(error_file);
          
  dup2(stderr_fd, STDERR_FILENO);
  // Closing of standard input files is illegal.
  if (input_fd != STDIN_FILENO) {
    fclose(input_file);
    input_file = stdin;
  }
  if (output_fd != STDOUT_FILENO) {
    fclose(output_file);
    output_file = stdout;
  }
  if (error_fd != STDERR_FILENO) {
    fclose(error_file);
    error_file = stderr;
  }
}
// Shell built-in commands
/** Change directory. */
void cd(char * path) {
  if (!path) {
    chdir(getenv("HOME"));
  } else {
    if (chdir(path) < 0) {
      fprintf(error_file, "%s%s: %s%s\n", BOLD_RED, path, strerror(errno), RESET);
    }
  }
}
/** Display all active jobs */
void jobs() {
  for (size_t i = 0; i < background_jobs_idx; i++) {
    printf("[%zu]+  %s", i, background_jobs[i].command);
  }
}
/** Set or get the file creation mask of the current process. */
void umask_impl(tcommand command) {
  mode_t mask_value;
  bool symbolic = false;
  char * mask, * endptr;

  for (size_t i = 0; i < (size_t) command.argc; i++) {
    if (strcmp(command.argv[i], "-S") == 0 || strcmp(command.argv[i], "--symbolic") == 0) {
      symbolic = true;
    }
  }
  if (symbolic) {
    mask_value = umask(022);
    umask(mask_value);
    print_symbolic_umask(mask_value);
    return;
  }
  mask = command.argv[1];
  if (mask) {
    errno = 0; 
    mask_value = strtol(mask, &endptr, 8);
    if ( * endptr != '\0' || errno) {
      print_color(
        error_file,
        BOLD_RED, 
        "umask: Error de conversión a octal. Nota: actualizar permisos de forma simbólica no está soportado actualmente.\n"
      );
      return; 
    }
    umask(mask_value);
    return;
  } else {
    mask_value = umask(022);
    umask(mask_value);
    fprintf(output_file, "%04o\n", mask_value); 
  }
}
/** Bring a background job to the foreground. */
void foreground(size_t job_id) {
  pid_t pid;

  if (job_id >= background_jobs_idx) {
    print_color(error_file, BOLD_RED, "fg: no hay ningún trabajo en segundo plano con ese identificador.\n");
    return;
  }
  pid = background_jobs[job_id].pid;
  if (pid == 0) {
    print_color(error_file, BOLD_RED, "fg: no hay ningún trabajo en segundo plano con ese identificador.\n");
    return;
  }
  foreground_job_pid = pid;
  fprintf(stdout, "Ejecutando en primer plano [%zu] %s", job_id, background_jobs[job_id].command);
  waitpid(pid, NULL, 0);
  foreground_job_pid = 0;
  remove_background_job(pid);
}
/** https://github.com/bminor/bash/blob/f3a35a2d601a55f337f8ca02a541f8c033682247/builtins/umask.def#L146
 * Prints the permissions mask similarly to how "ls -l" would. */
void print_symbolic_umask(mode_t umask) {
  char user_bits[4], group_bits[4], other_bits[4];
  int i;

  // S_IRUSR and friends are defined in sys/stat.h
  i = 0;
  if ((umask & S_IRUSR) == 0) user_bits[i++] = 'r';
  if ((umask & S_IWUSR) == 0) user_bits[i++] = 'w';
  if ((umask & S_IXUSR) == 0) user_bits[i++] = 'x';
  user_bits[i] = '\0';
  i = 0;
  if ((umask & S_IRGRP) == 0) group_bits[i++] = 'r';
  if ((umask & S_IWGRP) == 0) group_bits[i++] = 'w';
  if ((umask & S_IXGRP) == 0) group_bits[i++] = 'x';
  group_bits[i] = '\0';
  i = 0;
  if ((umask & S_IROTH) == 0) other_bits[i++] = 'r';
  if ((umask & S_IWOTH) == 0) other_bits[i++] = 'w';
  if ((umask & S_IXOTH) == 0) other_bits[i++] = 'x';
  other_bits[i] = '\0';
  // According to Wikipedia, there should be a fourth permission bit for "a" (all).
  // This is not the case for the C implementation of umask. 
  fprintf(output_file, "u=%s,g=%s,o=%s\n", user_bits, group_bits, other_bits);
}

