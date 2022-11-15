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
#define BACKGROUND_JOBS_MAX 10

// Function declarations
// Shell prompt helpers
void print_prompt();
void print_green(char * str);
void print_blue(char * str);
void print_yellow(char * str);
void print_red(char * str);
char * polite_directory_format(char * name);
// Signal handlers
void sigint_handler(int sig);
void sigchld_handler(int sig);
// Shell helpers
void parse_shell_args(int argc, char ** argv);
char * parse_tcommand_to_string(tcommand * command, char * buffer);
void execute_command(tcommand * command);
// Shell commands
void cd(char * path);
void umask(tcommand * command);
void background(tcommand * command);
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


int main(int argc, char *argv[]) {
  char input_buffer[PATH_MAX];
  tline * tline;
  tcommand * tcommands; 
  tcommand command;

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
          command = tcommands[i];
          background(&command);
        }
      } else {
        for (int i = 0; i < tline->ncommands; i++) {
          command = tcommands[i];
          execute_command(&command);
        }
      }
    }
    print_prompt();
  }
  return 0;
}

// Shell prompt helpers
void print_prompt() {
  char prompt[PATH_MAX], 
       hostname[HOST_NAME_MAX];

  gethostname(hostname, sizeof(hostname));
  sprintf(prompt, "%s@%s", getenv("USER"), hostname);
  print_green(prompt);
  printf(":");
  getcwd(prompt, sizeof(prompt));
  print_blue(polite_directory_format(prompt));
  printf(" msh> ");
}
void print_green(char *str) {
  printf("\033[1;32m");
  printf("%s", str);
  printf("\033[0m");
}
void print_blue(char *str) {
  printf("\033[1;34m");
  printf("%s", str);
  printf("\033[0m");
}
void print_yellow(char *str) {
  printf("\033[1;33m");
  printf("%s", str);
  printf("\033[0m");
}
void print_red(char *str) {
  printf("\033[1;31m");
  printf("%s", str);
  printf("\033[0m");
}
// Del código fuente de Bash.
// https://github.com/bminor/bash/blob/fb0092fb0e7bb3121d3b18881f72177bcb765491/general.c
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
  
  bool element_found = false;
  for (size_t i = 0; i < background_jobs_idx && !element_found; i++) {
    element_found = background_jobs[i]->pid == pid;
    if (element_found) {
      if (i == background_jobs_idx) {
        background_jobs_idx--;
      } else {
        while (i < background_jobs_idx) {
          background_jobs[i] = background_[i + 1];
          i++;
        }
        background_jobs_idx--;
      }
    }
  }
}

// Shell helpers
void parse_shell_args(int argc, char ** argv) {
  if (argc > 1) {
    for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-s") == 0) {
        extended_support = true;
      }
    }
  }
  printf("Welcome to msh, the minimal shell. Type 'help' for more information.\n");
  if (!extended_support) {
    print_yellow("WARNING: the shell has been initialized without the -s option.\n");
    print_yellow("Only the commands 'cd', 'exit', 'fg', 'jobs' and 'umask' will be available.\n");
  }
}
char * parse_tcommand_to_string(tcommand * command, char * buffer) {
  strcpy(buffer, command->filename);
  for (int i = 0; i < command->argc; i++) {
    strcat(buffer, " ");
    strcat(buffer, command->argv[i]);
  }
  return buffer;
}
void execute_command(tcommand * command) {
  char error_message[FILENAME_MAX + 30];
  if (strcmp(command->filename, "cd") == 0) {
    cd(command->argv[1]);
  } else if (strcmp(command->filename, "umask") == 0) {
    umask(command);
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
      if (pid == 0) {
        execvp(command->filename, command->argv);
        sprintf(error_message, "%s: command not found", command->filename);
        perror(error_message);
        exit(1);
      } else {
        waitpid(pid, NULL, 0);
      }
    } else {
      sprintf(error_message, "msh: command not found: %s", command->filename);
      print_yellow("Consider using the -s option to enable extended support.\n");
      print_red(error_message);
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
void umask(tcommand * command) {
  umask()
}
void background(tcommand * command) {
  if (background_jobs_idx + 1 == BACKGROUND_JOBS_MAX) {
    print_red("Error: exceeded the maximum number of background jobs");
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
    background_pids[background_jobs_idx] = pid;
    ++background_jobs_idx;
  }
}
void foreground(pid_t pid) {
  int status;
  waitpid(pid, &status, 0);
}
void jobs() {
  for (size_t i = 0; i < background_jobs_idx; i++) {
    printf("[%d] %d\n", i, background_pids[i]);
  }
}