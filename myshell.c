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
#include "parser.h"
// Constant definitions
#define BACKGROUND_JOBS_MAX 10
#define WHITE "\033[0m"
#define GREEN "\x1b[32m"
#define BLUE "\x1b[34m"
#define YELLOW "\x1b[33m"
#define RED "\x1b[31m"
#define RESET "\x1b[0m"

// Function declarations
// Shell prompt helpers
void print_prompt();
void print_color(FILE * stream, char * color, char * string);
char * polite_directory_format(char * name);
// Signal handlers
void sigint_handler(int sig);
void sigchld_handler(int sig);
// Shell helpers
void parse_shell_args(int argc, char ** argv);
void parse_tcommand_to_string(tcommand * command, char * buffer);
void execute_commands(tcommand ** commands);
void kill_job(pid_t pid);
// Shell commands
void cd(char * path);
void _umask(char * mask);
void background(tcommand ** commands);
void foreground(pid_t pid);
void jobs();

// Type definitions
typedef struct {
    pid_t pid;
    char * command;
} background_job;

// Global variables
pid_t current_pid;                           // PID of the current process being executed by the shell
background_job background_jobs[BACKGROUND_JOBS_MAX];
size_t background_jobs_idx = 0;              // Index of the next available position in background_jobs
bool extended_support = false;               // Flag to indicate if other shell commands should be executed
// Variables for output redirection
FILE * input_file;                    // File to redirect input from
FILE * output_file;                   // File to redirect output to
FILE * error_file;                    // File to redirect error to

int main(int argc, char *argv[]) {
  char input_buffer[PATH_MAX];
  tline * tline;
  tcommand * tcommands; 
  tcommand command;

  input_file = stdin;
  output_file = stdout;
  error_file = stderr;
  parse_shell_args(argc, argv);
  signal(SIGINT, sigint_handler);
  signal(SIGCHLD, sigchld_handler);
  print_prompt();
  while (fgets(input_buffer, PATH_MAX, stdin)) {
    tline = tokenize(input_buffer);
    if (tline->ncommands > 0) {
      tcommands = tline->commands;
      if (tline->background) {
        for (int i = 0; i < tline->ncommands; i++) {
          background(tcommands);
        }
      } else {
        execute_commands(tcommands);
        print_prompt();
      }
    }
  }
  return 0;
}

// Shell prompt helpers
void print_prompt() {
  char prompt[PATH_MAX], 
       hostname[HOST_NAME_MAX];

  gethostname(hostname, sizeof(hostname));
  sprintf(prompt, "%s@%s", getenv("USER"), hostname);
  print_color(stdout, GREEN, prompt);
  printf(":");
  getcwd(prompt, sizeof(prompt));
  print_color(stdout, BLUE, polite_directory_format(prompt));
  printf(" msh> ");
}
void print_color(FILE * stream, char * color, char * string) {
  fprintf(stream, "%s%s%s", color, string, RESET);
}
char * polite_directory_format(char * name) {
  // Del código fuente de Bash.
  // https://github.com/bminor/bash/blob/fb0092fb0e7bb3121d3b18881f72177bcb765491/general.c
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
void sigint_handler(int sig) {
  print_prompt();
}
void sigchld_handler(int sig) {
  int status;
  pid_t pid;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    if (WIFEXITED(status)) {
      printf("Process %d exited with status %d", pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      printf("Process %d killed by signal %d", pid, WTERMSIG(status));
    } else if (WIFSTOPPED(status)) {
      printf("Process %d stopped by signal %d", pid, WSTOPSIG(status));
    } else if (WIFCONTINUED(status)) {
      printf("Process %d continued", pid);
    }
  }
}

// Shell helpers
void parse_shell_args(int argc, char ** argv) {
  if (argc > 1) {
    for (size_t i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-s") == 0) {
        extended_support = true;
      }
    }
  }
  if (!extended_support) {
    print_color(stdout, YELLOW, "WARNING: the shell has been initialized without the -s option.\n");
    print_color(stdout, YELLOW, "Only the commands 'cd', 'exit', 'fg', 'jobs' and 'umask' will be available.\n");
  }
}
void parse_tcommand_to_string(tcommand * command, char * buffer) {
  size_t length; 

  length = strlen(command->filename + 1);
  for (size_t i = 0; i < command->argc; ++i) {
    length += strlen(command->argv[i]); + 1;
  }
  buffer = (char *) malloc(length * sizeof(char));
  strcpy(buffer, command->filename);
  for (size_t i = 0; i < command->argc; i++) {
    strcat(buffer, " ");
    strcat(buffer, command->argv[i]);
  }
}
void execute_command(tcommand * command) {
  char error_message[FILENAME_MAX + 30];

  if (strcmp(command->filename, "cd") == 0) {
    cd(command->argv[1]);
  } else if (strcmp(command->filename, "umask") == 0) {
    _umask(command->argv[0]);
  } else if (strcmp(command->filename, "jobs") == 0) {
    jobs();
  } else if (strcmp(command->filename, "fg") == 0) {
    foreground(atoi(command->argv[1]));
  } else if (strcmp(command->filename, "exit") == 0) {
    exit(0);
  } else {
    if (extended_support) {
      // try to execute the command as a linux command
      pid_t pid = fork();
    } else {
      sprintf(error_message, "msh: command not found: %s", command->filename);
      print_color(error_file, YELLOW, "Consider using the -s option to enable extended support.\n");
      print_color(error_file, RED, error_message);
    }
  }
}

void kill_job(pid_t pid) {
  bool element_found = false;
  for (size_t i = 0; i < background_jobs_idx && !element_found; i++) {
    element_found = background_jobs[i].pid == pid;
    if (element_found) {
      free(background_jobs[i].command);
      if (i == background_jobs_idx) {
        background_jobs_idx--;
      } else {
        while (i < background_jobs_idx) {
          background_jobs[i] = background_jobs[i + 1];
          i++;
        }
        background_jobs_idx--;
      }
    }
  }
}

// Shell commands
void cd(char * path) {
  if (path == NULL) {
    chdir(getenv("HOME"));
  } else {
    chdir(path);
  }
}

void _umask (char * mask) {
  mode_t mask_value;
  char mask_string[4];

  if (mask != NULL) {
    mask_value = strtol(mask, NULL, 8);
    umask(mask_value);
    return;
  } else {
    mask_value = umask(022);
    umask(mask_value);
    sprintf(mask_string, "%03o", mask_value);
    print_color(output_file, WHITE, mask_string);       
  }
}

void background(tcommand ** command) {
  if (background_jobs_idx + 1 == BACKGROUND_JOBS_MAX) {
    print_color(error_file, RED, "Error: exceeded the maximum number of background jobs");
    return;
  }
  pid_t pid = fork();
  if (pid == 0) {
    // Child process
    signal(SIGINT, SIG_DFL);
    tline * tline = tokenize(command);
    exit(0);
  } else {
    // Parent process
    background_jobs[background_jobs_idx].pid = pid;
    parse_tcommand_to_string(command, background_jobs[background_jobs_idx].command);
    ++background_jobs_idx;
  }
}
void foreground(pid_t pid) {
  int status;
  waitpid(pid, &status, 0);
}
void jobs() {
  for (size_t i = 0; i < background_jobs_idx; i++) {
    printf("[%d] %s\n", background_jobs[i].pid, background_jobs[i].command);
  }
}