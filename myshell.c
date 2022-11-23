#include <stdio.h>
#include <string.h>
#include <unistd.h> // getcwd(), gethostname()
#include <limits.h> // PATH_MAX
#include <stdlib.h> // exit(), getenv()
#include "parser.h"

// Shell prompt helpers
void print_prompt();
void print_green(char * str);
void print_blue(char * str);
char * polite_directory_format(char * name);


int main(int argc, char *argv[]) {
  char input_buffer[PATH_MAX];
  tline * tline;

  print_prompt();
  while (fgets(input_buffer, PATH_MAX, stdin)) {
    tline = tokenize(input_buffer);
    if (tline->ncommands > 0) {
      if (strcmp(tline->commands[0].argv[0], "exit") == 0) {
        exit(0);
      }
      if (strcmp(tline->commands[0].argv[0], "cd") == 0) {
        if (tline->commands[0].argc == 1) {
          chdir(getenv("HOME"));
        } else {
          chdir(tline->commands[0].argv[1]);
        } 
      }
      execl(tline->commands[0].filename, tline->commands[0].argv[0], NULL);
    }
    print_prompt();
  }
  return 0;
}

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

void print_color(FILE * stream, char * color, char * string) {
  fprintf(stream, "%s%s%s", color, string, RESET);
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