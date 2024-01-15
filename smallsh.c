// Kristy Waters
// OSU Small Shell Project

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

char *words[MAX_WORDS];
char *parsed[MAX_WORDS];
size_t wordsplit(char const *line);
char param_scan(char const *word, char **start, char **end);
char *build_str(char const **start, char const **end);
char *expand(char const *word);

char *empty_string = "";
int last_status;
int exit_status;
pid_t last_bg;
pid_t last_fg;
bool to_background = false;
pid_t bg_pid_list[128] = { 0 }; 
int num_bg = 0;
int new_num_bg = 0;
int in_file = 1;
int out_file = 1;

// Empty SIGINT handler function to avoid exiting
void handle_SIGINT(int signo){}

// Main function call for Shell
int main(int argc, char *argv[])
{ 
  // Declare signal action structs 
  struct sigaction SIGINT_old_action = {0}, SIGTSTP_old_action = {0}, SIGINT_action = {0}, ignore_action = {0};

  // Set PS1 Variable if not already set. 
  if (getenv("PS1") == NULL){
    setenv("PS1", "$ ", 1);
  }

  // PROVIDED CODE //
  FILE *input = stdin;
  char *input_fn = "(stdin)";

  // Case if file input provided
  if (argc == 2) {
    input_fn = argv[1];
    input = fopen(input_fn, "re");
    if (!input) err(1, "%s", input_fn);
    fcntl(input, F_SETFD, FD_CLOEXEC);
  } 
  
  else if (argc > 2) {
    errx(1, "too many arguments");
  } 

  if (input == stdin){
    ignore_action.sa_handler = SIG_IGN;
    SIGINT_action.sa_handler = handle_SIGINT;

    sigaction(SIGINT, &ignore_action, &SIGINT_old_action);
    sigaction(SIGTSTP, &ignore_action, &SIGTSTP_old_action);

  }

  char *line = NULL;
  size_t n = 0;
  // END OF PROVIDED CODE //


  // -- INFINITE LOOP INITIATED -- //
  for (;;) {

bgcheck:;
  /* Manage background processes */
  if (num_bg > 0){
    new_num_bg = num_bg;

    // Loop through all of the background processes
    for (int i = 0; i < new_num_bg; i++){
      // Do not update last bg pid in this section
      int cur_status;
      bg_pid_list[i] = waitpid(bg_pid_list[i], &cur_status, WNOHANG | WUNTRACED);
      
      // Check if background process exited
      if (WIFEXITED(cur_status) && bg_pid_list[i] != 0){
        exit_status = WEXITSTATUS(cur_status);
        fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t) bg_pid_list[i], exit_status);
        exit_status = 0;
      } 

      // Check if background process was signaled
      else if (WIFSIGNALED(cur_status) && bg_pid_list[i] != 0){
        if (cur_status > 128) cur_status = cur_status - 128;
        fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t) bg_pid_list[i], cur_status);
      }

      // Check if background process was stopped
      else if (WIFSTOPPED(cur_status) && bg_pid_list[i] != 0){
        fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) bg_pid_list[i]);
        kill(bg_pid_list[1], SIGCONT);
      }
    
      // If it ended, remove from background pid list
      if (bg_pid_list[i] == 0){
        free(bg_pid_list[i]);
        // Swap last item in list with now empty value
        bg_pid_list[i] = bg_pid_list[new_num_bg];
        // Decrement so that this position is processed again with new value
        i--;
        new_num_bg--;
      }
    }
  }

prompt:;
    /* Prompt: expansion of PS1 variable (default: "$ ") */ 
    if (input == stdin) {
      fprintf(stderr, "%s", getenv("PS1"));

      // Set handlers
      ignore_action.sa_handler = SIG_IGN;
      SIGINT_action.sa_handler = handle_SIGINT;

      // Install signal handler to have SIGINT do nothing during read
      sigaction(SIGINT, &SIGINT_action, NULL);
      sigaction(SIGTSTP, &ignore_action, NULL);
    }

inpexp:;
    // -- IF FILE PROVIDED -> NOT STDIN -- DOES NOT PROMPT -- //

    // Get line from stdin OR read from file
    ssize_t line_len = getline(&line, &n, input);
    
    if (input == stdin){
    // Install signal handler to ignore SIGINT
    sigaction(SIGINT, &ignore_action, NULL);
    }

    // Getline returns -1 in 3 situations
    if (line_len <= 0){

      // 1) End of file reached
      if (feof(input)) return 0;  
      
      // 2) File read interrupted
      if (line_len == -1){
        clearerr(stdin);
        errno = 0;
        fprintf(stderr, "\n");
        goto bgcheck;
      }

      // 3) File read error
      else err(1, "%s", input_fn);
    }

    // -- PROVIDED FUNCTIONALITY -- //
    // Call Wordsplit function to split input into words
    size_t nwords = wordsplit(line);

    // Handle if "enter" is only input
    if (nwords == 0) goto bgcheck;

    // Expand each token of input
    for (size_t i = 0; i < nwords; ++i) {
      char *exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;

      // Indicate background process for exit/cd commands
      if (strcmp(words[i], "&") == 0) {
          to_background = true;
      } 
    }
     // -- END OF PROVIDED FUNCTIONALITY -- //

    // Handle if no commands are provided to input
    if (words[0] == NULL) goto bgcheck;
    
checkcustom:; 
    // Check if command is a custom command "exit" or "cd"
    int result = strcmp(words[0], "exit");
    int result2 = strcmp(words[0], "cd");

    // First command is "exit"
    if (result == 0) {

      // No exit status is provided
      if (nwords == 1) {
        last_fg = 0;
        exit(atoi(expand("$?")));
      }

      // If exit status is provided
      else if (nwords == 2){
        last_fg = atoi(words[1]);
        exit(last_fg);
      }

      // If background operator is supplied
      else if (nwords == 3 && to_background){
        last_bg = atoi(words[1]);
        exit_status = last_bg;
        bg_pid_list[num_bg] = getpid();
        num_bg++;
        to_background = false;
        exit(last_bg);
      }
  
      // Erraneous exit command provided
      else {
        perror("Error exiting");
      }

      goto bgcheck;
    }
    
    // First command is "cd"
    else if (result2 == 0) {
      
      // No directory specified -- return to HOME
      if (nwords == 1) {
        chdir(getenv("HOME"));
      } 

      // Direcotry change error
      else if (nwords > 2){
        perror("Chdir Error");
      }

      // Change to input specified folder
      else {
        int directory = chdir(words[1]);
        if (directory != 0) {
          perror("Directory change error. \n");
        }
      }

      goto bgcheck;
    }

execute:;
    // If parsed statement NOT custom command, execute via execvp
    int   childStatus;
    pid_t childPid = fork();

    switch (childPid) {

    // If fork unsuccessful, returns -1
    case -1:
      perror("fork() failed!");
      exit(1);
      break;

    // If fork successful, returns 0
    case 0:
      // Reset signal handlers
      if (input == stdin){
        sigaction(SIGINT, &SIGINT_old_action, NULL);
        sigaction(SIGTSTP, &SIGTSTP_old_action, NULL);
      }

      parse:;
      // Parse line segments array into Tokens
      char *parsed[MAX_WORDS] = {'\0'};
      int parsed_count = 0;

      // Cycle through words array and parse each word
      for (size_t i = 0; i < nwords; ++i) {

        // Run process in background
        if (strcmp(words[i], "&") == 0) {
          to_background = true;
        }  

        // Open File to read
        else if (strcmp(words[i], "<") == 0) {
          in_file = open(words[i+1], O_RDONLY);

          // Error handle open funtion
          if (in_file == -1) { 
            perror("source open()"); 
            exit(1); 
          }
          // Redirect stdin to source file
          dup2(in_file, 0);
          i++;
        }  

        // Open File to output to (overwrite)
        else if (strcmp(words[i], ">") == 0) {
          out_file = open(words[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0777);

          // Error handle open funtion
          if (out_file == -1) { 
            perror("destination open()"); 
            exit(1); 
          }
          // Redirect stdout to outfile
          dup2(out_file, 1);
          // Skip this character from parsed list
          i++;
        }  
        
        // Open File to output to (append)
        else if (strcmp(words[i], ">>") == 0) {
          out_file = open(words[i+1], O_WRONLY | O_CREAT | O_APPEND, 0777);
          if (out_file == -1) { 
            perror("destination open()"); 
            exit(1); 
          }
          // Redirect stdout to outfile (append)
          dup2(out_file, 1);
          // Skip this character from parsed list
          i++;
        }  
        
        // Copy word into parsed array
        else {
          parsed[parsed_count] = strdup(words[i]);
          parsed_count++;
        }
      }

      // DO NOT UPDATE STATUS HERE, just call execvp
      execvp(parsed[0], parsed);
      perror("Execve Error\n");
      exit(2);
      break;

    default: 

      // Check if background command
      if (to_background == true) {
        last_bg = childPid;
        // Non blocking wait
        childPid = waitpid(childPid, &childStatus, WNOHANG | WUNTRACED); 
        bg_pid_list[num_bg] = childPid;
        num_bg++; 
        to_background = false;
      }
    
      // Run in foreground
      else{
        last_fg = childPid;
        // Blocking wait
        childPid = waitpid(childPid, &childStatus, WUNTRACED | WSTOPPED | 0 );

        // If child status exited
        if (WIFEXITED(childStatus)){
          last_fg = WEXITSTATUS(childStatus);
        } 

        // If child status signaled
        else if (WIFSIGNALED(childStatus)){
          last_fg = WTERMSIG(childStatus) + 128;
        }

        // If child status stopped, continue
        else if (WIFSTOPPED(childStatus)){
          last_bg = childPid;
          fprintf(stderr, "Child process %d stopped. Continuing.\n", childPid);
          kill(childPid, SIGCONT);
        }
      }

      break;
    }

    // Close any open files (from < > >> operators)
    if (in_file == 0) close(in_file);
    in_file = 1;
    if (out_file == 0) close(out_file);
    out_file = 1;
  }

return 0;
}


// -- SKELETON CODE FUNCTIONS PROVIDED BELOW -- //

/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char const *line) {
  size_t wlen = 0;
  size_t wind = 0;

  char const *c = line;
  for (;*c && isspace(*c); ++c); /* discard leading space */

  for (; *c;) {
    if (wind == MAX_WORDS) break;
    /* read a word */
    if (*c == '#') break;
    for (;*c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
      if (!tmp) err(1, "realloc error in wordsplit");
      words[wind] = tmp;
      words[wind][wlen++] = *c; 
      words[wind][wlen] = '\0';
    }
    ++wind;
    wlen = 0;
    for (;*c && isspace(*c); ++c);
  }
  return wind;
}


/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char param_scan(char const *word, char **start, char **end)
{
  static char const *prev;
  if (!word) word = prev;
  
  char ret = 0;
  *start = 0;
  *end = 0;

  for (char const *s = word; *s && !ret; ++s) {
    s = strchr(s, '$');
    if (!s) break;
    switch (s[1]) {
    case '$':
    case '!':
    case '?':
      ret = s[1];
      *start = s;
      *end = s + 2;
      break;
    case '{':;
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = s[1];
        *start = s;
        *end = e + 1;
      }
      break;
    }
  }
  prev = *end;
  return ret;
}


/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *build_str(char const **start, char const **end)
{
  static size_t base_len = 0;
  static char *base = 0;

  if (!start) {
    /* Reset; new base string, return old one */
    char *ret = base;
    base = NULL;
    base_len = 0;
    return ret;
  }

  /* Append [start, end) to base string 
   * If end is NULL, append whole start string to base string.
   * Returns a newly allocated string that the caller must free.
   */
  size_t n = end ? end - start : strlen(start);
  size_t newsize = sizeof *base *(base_len + n + 1);
  void *tmp = realloc(base, newsize);
  if (!tmp) err(1, "realloc");
  base = tmp;
  memcpy(base + base_len, start, n);
  base_len += n;
  base[base_len] = '\0';

  return base;
}
// -- END OF PROVIDED SKELETON CODE -- //


/* Expands all instances of $! $$ $? and ${param} in a string 
 * Returns a newly allocated string that the caller must free
 * This has been modified from provided skeleton code
 */
char * expand(char const *word)
{
  char const *pos = word;
  char *start, *end;
  char c = param_scan(pos, &start, &end);
  build_str(NULL, NULL);
  build_str(pos, start);

  while (c) {
    if (c == '!') {
      char* bgpid_str[10];
      // Defaults to empty string ""
      sprintf( bgpid_str, "%jd", (intmax_t) last_bg );
      strcat(bgpid_str, "\0");
      build_str(&bgpid_str, NULL);
    }

    else if (c == '$') {
      char* pid_str[10];
      sprintf( pid_str, "%jd", (intmax_t) getpid() );
      strcat(pid_str, "\0");
      build_str(&pid_str, NULL);
    }

    else if (c == '?') {
      char* status_str[6];
      // Defaults to "0"
      sprintf( status_str, "%jd", (intmax_t) last_fg );
      strcat(status_str, "\0");
      build_str(&status_str, NULL);
    }

    else if (c == '{') {
      char buff[1024] = {'\0'};
      int len = (end-1) - (start+2);
      strncpy(buff, start+2, len);
      strcat(buff, "\0");
      char *path;
      // Defaults to empty string ""
      if (getenv(buff) == NULL) path = "";
      else path = getenv(buff);
      build_str(path, NULL);
    }

    pos = end;
    c = param_scan(pos, &start, &end);
    build_str(pos, start);
  }
  
  return build_str(start, NULL);
}
