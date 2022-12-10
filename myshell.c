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
#include "parser.h"
// Constant definitions
#define BACKGROUND_JOBS_MAX 10
#define WHITE "\x1b[0m"
#define GREEN "\x1b[32m"
#define BLUE "\x1b[34m"
#define YELLOW "\x1b[33m"
#define RED "\x1b[31m"
#define RESET "\x1b[0m"

// Shell prompt helpers

void print_prompt();
void print_color(FILE * stream, char * color, char * text);
char * polite_directory_format(char * name);

// Signal handlers

void sigint_handler(int sig);
void sigchld_handler(int sig);

// Shell helpers

void parse_shell_args(int argc, char ** argv);
bool set_redirection_variables(tline * line);
bool cd_or_exit_are_present(tline * line);
void parse_tcommand_to_string(tcommand * command, char * buffer);
void execute_commands(tcommand ** commands, bool background);
void close_redirection_files();
void kill_job(pid_t pid);

// Shell commands

void cd(char * path);
void _umask(char * mask);
void foreground(size_t job_id);
void jobs();

// Type definitions
/** PID and string representation of a job. */
typedef struct {
  pid_t pid;
  char * command;
} background_job;

// Global variables
background_job background_jobs[BACKGROUND_JOBS_MAX]; // List of background jobs
size_t background_jobs_idx = 0;                      // Index of the next available position in background_jobs
bool extended_support = false;                       // Flag to indicate if other shell commands should be executed
// Variables for output redirection
FILE * input_file;                                   // File to redirect input from
FILE * output_file;                                  // File to redirect output to
FILE * error_file;                                   // File to redirect error to

int main(int argc, char *argv[]) {
  char input_buffer[PATH_MAX];
  char * bad_command;
  tline * line;

  parse_shell_args(argc, argv);
  signal(SIGINT, sigint_handler);
  signal(SIGCHLD, sigchld_handler);
  print_prompt();
  while (fgets(input_buffer, PATH_MAX, stdin)) {
    line = tokenize(input_buffer);
    set_redirection_variables(line); // Set redirection variables
    bad_command = check_if_all_commands_are_valid(line);
    if (bad_command != NULL) {
      print_color(error_file, RED, "msh: command not found: ");
      print_color(error_file, RED, bad_command);
      print_color(error_file, RED, "\n");
      print_prompt();
      continue;
    }
    if (line->ncommands > 1) {
      if(cd_or_exit_are_present(line)) {
        print_color(error_file, RED, "msh: cd and exit cannot be used with pipes\n");
        print_prompt();
        continue;
      }
      execute_commands(line->commands, line->background);
    } else {
      if (strcmp(line->commands[0].argv[0], "cd") == 0) {
        cd(line->commands[0].argv[1]);
      } else if (strcmp(line->commands[0].argv[0], "umask") == 0) {
        _umask(line->commands[0].argv[1]);
      } else if (strcmp(line->commands[0].argv[0], "fg") == 0) {
        foreground(atoi(line->commands[0].argv[1]));
      } else if (strcmp(line->commands[0].argv[0], "jobs") == 0) {
        jobs();
      } else if (strcmp(line->commands[0].argv[0], "exit") == 0) {
        exit(0);
      } else {
        execvp(line->commands[0].argv[0], line->commands[0].argv);
      }
    }
    close_redirection_files();
  }
  return 0;
}

// Shell prompt helpers 

/** Print the msh> prompt. */
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
// Print a string in a given color to a stream.
void print_color(FILE * stream, char * color, char * string) {
  fprintf(stream, "%s%s%s", color, string, RESET);
}
/** https://github.com/bminor/bash/blob/fb0092fb0e7bb3121d3b18881f72177bcb765491/general.c
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

/** Handles SIGINT: ignore, flush and print prompt. */ 
void sigint_handler(int sig) {
  fflush(stdin);
  print_prompt();
}
/**  Handles SIGCHLD: remove terminated background jobs from the list,
 *   and print a message if the job was terminated by a signal. */
void sigchld_handler(int sig) {
  int status;
  pid_t pid;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    if (WIFEXITED(status)) {
      printf("Process %d exited with status %d\n", pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      printf("Process %d killed by signal %d\n", pid, WTERMSIG(status));
    } else if (WIFSTOPPED(status)) {
      printf("Process %d stopped by signal %d\n", pid, WSTOPSIG(status));
    } else if (WIFCONTINUED(status)) {
      printf("Process %d continued\n", pid);
    }
  }
  signal(SIGCHLD, sigchld_handler);
}

// Shell helpers

/** Enable extended mode if the "-e" flag is present in the arguments. */
void parse_shell_args(int argc, char ** argv) {
  if (argc > 1) {
    for (size_t i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-e") == 0) {
        extended_support = true;
      }
    }
  }
  if (!extended_support) {
    print_color(stdout, YELLOW, "WARNING: the shell has been initialized without the -e option.\n");
    print_color(stdout, YELLOW, "Only the commands 'cd', 'exit', 'fg', 'jobs' and 'umask' will be available.\n");
  }
}
/** Set input, output and error redirection. 
 *  If they're not enabled, reset them to stdin, stdout and stderr. */ 
bool set_redirection_variables(tline * line) {
  FILE * file;

  input_file = stdin;
  output_file = stdout;
  error_file = stderr;
  // This code is tedious and repetitive. It needs to be moved to a function.
  if (line->redirect_input) {
    file = fopen(line->redirect_input, "r");
    if (file == NULL) {
      print_color(error_file, RED, line->redirect_input);
      print_color(error_file, RED, ": ");
      print_color(error_file, RED, strerror(errno));
      return false;
    }
    input_file = file;
  }
  if (line->redirect_output) {
    file = fopen(line->redirect_output, "w");
    if (file == NULL) {
      print_color(error_file, RED, line->redirect_output);
      print_color(error_file, RED, ": ");
      print_color(error_file, RED, strerror(errno));
      return false;
    }
    output_file = file;
  }
  if (line->redirect_error) {
    file = fopen(line->redirect_error, "w");
    if (file == NULL) {
      print_color(error_file, RED, line->redirect_error);
      print_color(error_file, RED, ": ");
      print_color(error_file, RED, strerror(errno));
      return false;
    }
    error_file = file;
  }
}
/** Determines if any of the commands entered is not valid. */ 
char * check_if_all_commands_are_valid(tline * line) {
  if (line == NULL) return NULL;
  for (size_t i = 0; i < line->ncommands; i++) {
    if (line->commands[i].filename == NULL) {
      return line->commands[i].argv[0];
    }
  }
  return NULL;
}
/**  Determines if the line contains the commands "cd" or "exit". */
bool cd_or_exit_are_present(tline * line) {
  for (size_t i = 0; i < line->ncommands; i++) {
    if (strcmp(line->commands[i].argv[0], "cd") == 0 || strcmp(line->commands[i].argv[0], "exit") == 0) {
      return true;
    }
  }
  return false;
}
/** Parse a tcommand to string to display it when jobs() is called. */ 
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
/** Execute the commands */
void execute_commands(tcommand ** commands, bool background) {
  pid_t pid;
  int status;
  char ** argv;

  argv = (char **) malloc((commands[0]->argc) * sizeof(char *));
  for (size_t i = 0; i < commands[0]->argc - 1; i++) {
    argv[i] = commands[0]->argv[i + 1];
  }

  pid = fork();
  if (pid == 0) {
    // Child process
    if (background) {
      setpgid(0, 0);
    }
    if (execvp(commands[0]->filename, argv) == -1) {
      print_color(error_file, RED, commands[0]->filename);
      print_color(error_file, RED, ": ");
      print_color(error_file, RED, strerror(errno));
      exit(EXIT_FAILURE);
    }
  } else if (pid < 0) {
    // Error forking
    print_color(error_file, RED, "fork(): no se pudo crear un nuevo proceso");
    exit(EXIT_FAILURE);
  } else {
    // Parent process
    if (background) {
      background_jobs[background_jobs_idx].pid = pid;
      parse_tcommand_to_string(commands[0], background_jobs[background_jobs_idx].command);
      background_jobs_idx++;
    } else {
      waitpid(pid, &status, 0);
    }
  }
}
/** Close redirection files if they're not stdin, stdout or stderr. */
void close_redirection_files() {
  if (input_file != stdin) {
    fclose(input_file);
  }
  if (output_file != stdout) {
    fclose(output_file);
  }
  if (error_file != stderr) {
    fclose(error_file);
  }
}

// Shell commands
/** Change directory. */
void cd(char * path) {
  if (path == NULL) {
    chdir(getenv("HOME"));
  } else {
    chdir(path);
  }
}
/** Display all active jobs */
void jobs() {
  for (size_t i = 0; i < background_jobs_idx; i++) {
    printf("[%d]+  %s\n", i, background_jobs[i].command);
  }
}
/** Set or get the file creation mask of the current process. */
void _umask (char * mask) {
  mode_t mask_value;
  char mask_string[4];
  char * endptr;

  if (mask != NULL) {
    mask_value = strtol(mask, &endptr, 8);
    if (endptr != '\0') {
      print_color(error_file, RED, "umask: introduzca un número en octal.\n");
      return;
    }
    umask(mask_value);
    return;
  } else {
    mask_value = umask(022);
    umask(mask_value);
    sprintf(mask_string, "%03o", mask_value);
    print_color(output_file, WHITE, mask_string);       
  }
}