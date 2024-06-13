#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>

#define MAX_COMMAND_LENGTH 511
#define MAX_NUM_ARGS 11
#define MAX_NUM_COMMANDS 11
#define MAX_VAR_NAME_LENGTH 100
#define MAX_VAR_VALUE_LENGTH 100

pid_t pid;
pid_t stp;
int outputFileFd;
int argCounter = 0;

typedef struct
{
  char name[MAX_VAR_NAME_LENGTH];
  char value[MAX_VAR_VALUE_LENGTH];
} variable;

variable *variables = NULL;
int numOfVariables = 0;
int maxVariables = 0;

// Function prototypes
void assignVariable(char *name, char *value);
char *getVariable(char *name);
void replaceVariables(char *input);
int extractCommands(char *input, char *commands[MAX_NUM_COMMANDS]);
void signal_handler(int signum);
int writeCommandToFile(char *command);
int countPipes(char input[], char *commands[]);
void handleInput(char *input, int *nullCommandCount, const int *cmd);
void runCommands(char **array, char **commands, char **args, int *cmd);
void executeCommand(char **commands, char **args, int *pipes, int numOfPipes, int *cmd);
void parseCommand(char *command, char **args, int *bg);
void countArgs(char **commands);
void cleanUpResources(char **array, int whileIndex, int *pipes);
void cleanupVariables();

/**
 * Adds a new variable to the variables array.
 * If the array is full, it doubles its size.
 *
 * @param name The name of the variable.
 * @param value The value of the variable.
 */
void assignVariable(char *name, char *value)
{
  if (numOfVariables >= maxVariables)
  {
    maxVariables = maxVariables == 0 ? 1 : maxVariables * 2;
    variable *temp = realloc(variables, maxVariables * sizeof(variable));
    if (temp == NULL)
    {
      perror("Error: Failed to allocate memory for variables");
      free(variables);
      exit(EXIT_FAILURE);
    }
    variables = temp;
  }

  strcpy(variables[numOfVariables].name, name);
  strcpy(variables[numOfVariables].value, value);
  numOfVariables++;
}

/**
 * Retrieves the value of a variable given its name.
 *
 * @param name The name of the variable.
 * @return The value of the variable, or NULL if the variable is not found.
 */
char *getVariable(char *name)
{
  for (int i = 0; i < numOfVariables; i++)
  {
    if (strcmp(variables[i].name, name) == 0)
    {
      return variables[i].value;
    }
  }
  return NULL;
}

/**
 * Replaces variables in the input string with their values.
 * Variables are prefixed with a '$' in the input string.
 *
 * @param input The input string containing variables.
 */
void replaceVariables(char *input)
{
  int varLen = 0;

  for (int i = 0; input[i] != '\0'; ++i)
  {
    int j = i + 1;
    if (input[i] == '$')
    {
      while (input[j] != '\0' && input[j] != '\"' && !isspace(input[j]))
      {
        varLen++;
        j++;
      }

      char *temp = malloc(varLen + 1);
      if (temp == NULL)
      {
        perror("Error: Failed to allocate memory for variable name");
        return;
      }
      memmove(temp, input + i + 1, varLen);
      temp[varLen] = '\0';

      char *value = getVariable(temp);
      if (value != NULL)
      {
        int valueLength = (int)strlen(value);
        memmove(input + i + valueLength, input + j, strlen(input + j) + 1);
        memcpy(input + i, value, valueLength);
        i += valueLength - 1;
      }
      else
      {
        while (input[i] != ' ' && input[i] != '\0')
        {
          input[i] = ' ';
          i++;
        }
      }

      varLen = 0;
      free(temp);
    }
  }
}

/**
 * Separates the input string into individual commands using ';' as a delimiter.
 * Handles quotes correctly, ensuring that semicolons inside quotes are not treated as delimiters.
 *
 * @param input The input string containing commands.
 * @param commands An array to store the separated commands.
 * @return The number of commands separated.
 */
int extractCommands(char *input, char *commands[MAX_NUM_COMMANDS])
{
  int i = 0;
  int j = 0;
  int inQuotes = 0;
  int numberOfCommands = 0;
  char command[MAX_COMMAND_LENGTH];

  while (input[i] != '\0')
  {
    if (input[i] == '"')
    {
      inQuotes = !inQuotes;
      i++;
    }

    if (input[i] == ';' && !inQuotes)
    {
      input[i] = '\0';
      command[j] = '\0';

      if (strlen(command) > 0)
      {
        commands[numberOfCommands] = malloc(strlen(command) + 1);
        if (commands[numberOfCommands] == NULL)
        {
          perror("Memory allocation failed");
          exit(EXIT_FAILURE);
        }
        strcpy(commands[numberOfCommands], command);
        numberOfCommands++;
      }

      if (numberOfCommands >= MAX_NUM_COMMANDS)
      {
        printf("Error: too many commands\n");
        exit(EXIT_FAILURE);
      }

      j = 0;
    }
    else
    {
      command[j] = input[i];
      j++;

      if (j >= MAX_COMMAND_LENGTH)
      {
        printf("Error: command too long\n");
        exit(EXIT_FAILURE);
      }
    }
    i++;
  }

  command[j] = '\0';
  if (strlen(command) > 0)
  {
    commands[numberOfCommands] = malloc(strlen(command) + 1);
    if (commands[numberOfCommands] == NULL)
    {
      perror("Memory allocation failed");
      exit(EXIT_FAILURE);
    }
    strcpy(commands[numberOfCommands], command);
    numberOfCommands++;
  }

  commands[numberOfCommands] = NULL;
  return numberOfCommands;
}

/**
 * Signal handler for child processes.
 * Catches SIGTSTP (stop signal) and stores the process ID of the stopped process.
 *
 * @param signum The signal number.
 */
void signal_handler(int signum)
{
  if (signum == SIGTSTP)
  {
    stp = pid;
  }
  waitpid(-1, NULL, WNOHANG);
}

/**
 * Checks for output redirection in a command.
 * Scans the command string for the '>' character indicating redirection and returns its index.
 *
 * @param command The command string.
 * @return The index of the '>' character if found, otherwise returns 0.
 */
int writeCommandToFile(char *command)
{
  for (int i = 0; i < strlen(command); ++i)
  {
    if (command[i] == '>')
    {
      return i;
    }
  }
  return 0;
}

/**
 * Counts the number of pipes in the input command and splits the command into separate commands.
 * Allocates memory for each command and handles error checking for memory allocation.
 *
 * @param input The input command string.
 * @param commands An array to store the separated commands.
 * @return The number of pipes found (i.e., number of commands - 1).
 */
int countPipes(char input[], char *commands[])
{
  int strLen = (int)strlen(input);
  int counter = 0;

  for (int i = 0; i < strLen; i++)
  {
    if (input[i] == '|')
    {
      counter++;
    }
  }
  counter++;

  int start = 0;
  int j = 0;

  for (int i = 0; i <= strLen; i++)
  {
    if (input[i] == '|' || input[i] == '\0')
    {
      int end = i;
      int tokenLen = end - start;

      commands[j] = (char *)malloc((tokenLen + 1) * sizeof(char));
      if (commands[j] == NULL)
      {
        perror("Memory allocation failed");
        for (int k = 0; k < j; k++)
        {
          free(commands[k]);
        }
        return 0;
      }
      strncpy(commands[j], input + start, tokenLen);
      commands[j][tokenLen] = '\0';

      start = i + 1;
      j++;
    }
  }

  for (int k = counter; k < MAX_NUM_COMMANDS; ++k)
  {
    commands[k] = NULL;
  }

  return counter - 1;
}

/**
 * Handles user input.
 * Reads input from the user, prints the command prompt, and handles consecutive null commands.
 * If three consecutive null commands are entered, the shell exits.
 *
 * @param input The input string.
 * @param nullCommandCount A pointer to the counter for consecutive null commands.
 * @param cmd The number of commands.
 */
void handleInput(char *input, int *nullCommandCount, const int *cmd)
{
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) == NULL)
  {
    perror("getcwd");
    exit(EXIT_FAILURE);
  }

  printf("#cmd:%d|#args:%d@%s ", *cmd, argCounter, cwd);
  fgets(input, MAX_COMMAND_LENGTH + 5, stdin);
  if (strlen(input) > MAX_COMMAND_LENGTH)
  {
    printf("Error too many characters");
    memset(input, '\0', MAX_COMMAND_LENGTH);
  }
  input[strcspn(input, "\n")] = '\0';
  if (strlen(input) == 0)
  {
    (*nullCommandCount)++;
    if (*nullCommandCount == 3)
    {
      cleanupVariables();
      exit(EXIT_SUCCESS);
    }
  }
  else
  {
    *nullCommandCount = 0;
  }
}

/**
 * Executes multiple commands separated by pipes.
 * Manages the creation and closing of pipes, and the execution of each command.
 *
 * @param array An array of commands to be executed.
 * @param commands An array to store the separated commands.
 * @param args An array to store the command arguments.
 * @param cmd A pointer to the command counter.
 */
void runCommands(char **array, char **commands, char **args, int *cmd)
{
  int j = 0;
  int whileIndex = 0;
  int numOfPipes;

  numOfPipes = countPipes(array[whileIndex], commands);
  int *pipes = NULL;
  if (numOfPipes > 0)
  {
    pipes = (int *)malloc(sizeof(int) * 2 * numOfPipes);
    for (int i = 0; i < numOfPipes; i++)
    {
      if (pipe(&(pipes[i * 2])) == -1)
      {
        perror("pipe");
        exit(EXIT_FAILURE);
      }
    }
  }

  while (array[whileIndex] != NULL)
  {
    if (commands[j] == NULL)
    {
      j = 0;
      numOfPipes = countPipes(array[whileIndex], commands);
      if (numOfPipes > 0)
      {
        pipes = (int *)malloc(sizeof(int) * 2 * numOfPipes);
        for (int i = 0; i < numOfPipes; i++)
        {
          if (pipe(&(pipes[i * 2])) == -1)
          {
            perror("pipe");
            exit(EXIT_FAILURE);
          }
        }
      }
    }
    executeCommand(commands, args, pipes, numOfPipes, cmd);
    whileIndex++;
    cleanUpResources(array, whileIndex, pipes);
  }
}

/**
 * Executes a single command.
 * Handles redirection, background execution, and forking of child processes.
 *
 * @param commands An array of commands to be executed.
 * @param args An array to store the command arguments.
 * @param pipes An array to store the pipes.
 * @param numOfPipes The number of pipes in the command.
 * @param cmd A pointer to the command counter.
 */
void executeCommand(char **commands, char **args, int *pipes, int numOfPipes, int *cmd)
{
  int j = 0;
  int redirIdx;

  while (commands[j] != NULL)
  {
    if (!strcmp(commands[j], "bg") || !strcmp(commands[j], " bg"))
    {
      commands[j] = NULL;
      kill(stp, SIGCONT);
      j++;
      continue;
    }
    redirIdx = writeCommandToFile(commands[j]);
    if (redirIdx > 0)
    {
      int k = (int)strlen(commands[j]) - redirIdx;
      char buff[k + 1];
      int executionCmdIndex = redirIdx + 1;
      int i = 0;
      while (i < k)
      {
        if (commands[j][executionCmdIndex] != ' ')
        {
          buff[i] = commands[j][executionCmdIndex];
          i++;
        }
        executionCmdIndex++;
      }
      buff[i] = '\0';
      commands[j][redirIdx] = '\0';
      outputFileFd = open(buff, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
    }

    int bg = 0;
    parseCommand(commands[j], args, &bg);

    if (pid == 0)
    {
      signal(SIGTSTP, SIG_DFL); // Reset to default handling in the child
      if (numOfPipes > 0 && pipes != NULL)
      {
        if (redirIdx > 0)
        {
          dup2(outputFileFd, STDOUT_FILENO);
        }
        if (j == 0)
        {
          if (dup2(pipes[1], STDOUT_FILENO) == -1)
          {
            perror("dup2");
            exit(EXIT_FAILURE);
          }
        }
        else if (j == numOfPipes)
        {
          if (dup2(pipes[(j - 1) * 2], STDIN_FILENO) == -1)
          {
            perror("dup2");
            exit(EXIT_FAILURE);
          }
        }
        else
        {
          if (dup2(pipes[(j - 1) * 2], STDIN_FILENO) == -1)
          {
            perror("dup2");
            exit(EXIT_FAILURE);
          }
          if (dup2(pipes[j * 2 + 1], STDOUT_FILENO) == -1)
          {
            perror("dup2");
            exit(EXIT_FAILURE);
          }
        }
      }
      if (redirIdx > 0)
      {
        dup2(outputFileFd, STDOUT_FILENO);
      }
      if (bg)
      {
        setpgid(0, 0);
      }

      if (execvp(args[0], args) == -1)
      {
        perror("execvp");
        exit(EXIT_FAILURE);
      }
      exit(EXIT_SUCCESS);
    }
    else
    {
      if (numOfPipes > 0 && pipes != NULL)
      {
        if (j > 0 && j < numOfPipes)
        {
          close(pipes[(j - 1) * 2]);
          close(pipes[j * 2 + 1]);
        }
        if (j == 0)
        {
          close(pipes[1]);
        }
        else if (j == numOfPipes)
        {
          close(pipes[(j - 1) * 2]);
        }
      }
      if (!bg)
      {
        waitpid(pid, NULL, WUNTRACED);
      }
    }
    j++;
    (*cmd)++;
  }
}

/**
 * Parses a command into arguments and handles variable assignments.
 * Splits the command string into individual arguments, handles quotes correctly, and processes variable assignments.
 *
 * @param command The command string to be parsed.
 * @param args An array to store the parsed arguments.
 * @param bg A pointer to the background execution flag.
 */
void parseCommand(char *command, char **args, int *bg)
{
  int isInQuotes = 0;
  char *tok = strtok(command, " ");
  int i = 0;

  while (tok != NULL)
  {
    if (!strcmp(tok, "cd"))
    {
      printf("cd not supported\n");
      tok = NULL;
      continue;
    }
    if (i >= MAX_NUM_ARGS)
    {
      printf("Too many arguments!!!\n");
      args[0] = NULL;
      break;
    }
    if (isInQuotes)
    {
      strcat(args[i - 1], " ");
      strcat(args[i - 1], tok);
    }
    else
    {
      char *equals = strchr(tok, '=');
      if (equals != NULL)
      {
        *equals = '\0';
        char *name = tok;
        char *value = equals + 1;
        assignVariable(name, value);
      }
      else
      {
        args[i] = tok;
        i++;
        argCounter++; // Increment the global argument counter
      }
    }

    if (tok[0] == '"')
    {
      isInQuotes = 1;
      memmove(tok, tok + 1, strlen(tok));
    }
    if (tok[strlen(tok) - 1] == '"')
    {
      isInQuotes = 0;
      tok[strlen(tok) - 1] = '\0';
    }
    if (isInQuotes)
    {
      tok = strtok(NULL, "\"");
    }
    else
    {
      tok = strtok(NULL, " ");
    }
  }

  if (i > 0 && args[i - 1] && args[i - 1][strlen(args[i - 1]) - 1] == '&')
  {
    *bg = 1;
    args[i - 1][strlen(args[i - 1]) - 1] = '\0';
  }

  args[i] = NULL;
}

/**
 * Counts the arguments in the commands and updates the global argument counter.
 * Iterates through the commands and counts the number of arguments.
 *
 * @param commands An array of commands to be counted.
 */
void countArgs(char **commands)
{
  for (int i = 0; commands[i] != NULL; i++)
  {
    char *copiedCommand = strdup(commands[i]);
    if (copiedCommand == NULL)
    {
      perror("strdup");
      exit(EXIT_FAILURE);
    }
    char *token = strtok(copiedCommand, " ");
    while (token != NULL)
    {
      argCounter++;
      token = strtok(NULL, " ");
    }
    free(copiedCommand);
  }
}

/**
 * Frees allocated memory for variables.
 */
void cleanupVariables()
{
  if (variables != NULL)
  {
    free(variables);
  }
}

/**
 * Frees allocated memory for commands and pipes.
 * Iterates through the array of commands and frees each allocated command string,
 * and also frees the pipes array if it is not NULL.
 *
 * @param array An array of commands.
 * @param whileIndex The number of commands to free.
 * @param pipes An array of pipes to free.
 */
void cleanUpResources(char **array, int whileIndex, int *pipes)
{
  for (int i = 0; i < whileIndex; i++)
  {
    free(array[i]);
  }
  if (pipes != NULL)
  {
    free(pipes);
  }
}

/**
 * Main function of the shell program.
 * Initializes necessary variables, sets up the signal handler,
 * and enters the main loop to handle user input and execute commands.
 */
int main()
{
  char input[MAX_COMMAND_LENGTH + 5];
  char *args[MAX_NUM_ARGS];
  char *commands[MAX_NUM_COMMANDS];
  char *array[MAX_NUM_COMMANDS];
  int cmd = 0;
  int nullCommandCount = 0;

  // Ignore SIGTSTP in the parent process
  struct sigaction sa;
  sa.sa_handler = SIG_IGN;
  sigaction(SIGTSTP, &sa, NULL);

  // Set up signal handler for child processes
  signal(SIGCHLD, signal_handler);

  // Initialize the args array
  for (int k = 0; k < 10; k++)
  {
    args[k] = NULL;
  }

  // Main loop to handle user input and execute commands
  while (1)
  {
    handleInput(input, &nullCommandCount, &cmd);
    if (strlen(input) == 0)
    {
      continue;
    }

    // Replace variables in the input
    replaceVariables(input);

    // Separate the input into individual commands
    int numberOfCommands = extractCommands(input, array);

    // Count the arguments in the commands
    countArgs(array);

    // Fork a child process to execute commands
    pid = fork();
    if (pid == -1)
    {
      perror("fork");
      exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
      // In child process
      signal(SIGTSTP, SIG_DFL); // Reset to default handling in the child
      runCommands(array, commands, args, &cmd);
      exit(EXIT_SUCCESS);
    }
    else
    {
      // In parent process
      waitpid(pid, NULL, WUNTRACED);
      // Update cmd count with the number of commands separated
      cmd += numberOfCommands;
    }
  }
}
