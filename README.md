
# Mini Shell

This repository contains a custom-built mini shell program implemented in C. The shell supports various standard functionalities including command execution, piping, redirection, and background process management.

## Key Features

- **Standard Command Execution**: Run typical shell commands.
- **Environment Variable Management**: Assign and substitute variables within commands.
- **Command Chaining**: Use semicolons (`;`) to chain multiple commands.
- **Piping**: Redirect output from one command to another using pipes (`|`).
- **Output Redirection**: Redirect command output to files using (`>`).
- **Background Processing**: Execute commands in the background using (`&`).
- **Signal Handling**: Handle signals for child processes to manage process states effectively.

## Getting Started

### Prerequisites

- Linux environment
- GCC compiler
- Standard C library


### Compilation

Compile the shell program using:
```bash
gcc -o mini_shell mini_shell.c
```

## Usage

Run the mini shell with the following command:
```bash
./mini_shell
```

### Commands

You can enter commands just like you would in a regular shell. Here are some examples:

- **Execute a command**:
    ```sh
    ls -l
    ```

- **Assign a variable**:
    ```sh
    VAR=value
    echo $VAR
    ```

- **Command chaining**:
    ```sh
    ls -l; echo "Hello World"
    ```

- **Piping**:
    ```sh
    ls -l | grep mini_shell
    ```

- **Output redirection**:
    ```sh
    echo "Hello World" > output.txt
    ```

- **Background process**:
    ```sh
    sleep 10 &
    ```

## Configuration

The mini shell supports dynamic variable assignment and usage within the shell environment. Variables can be set and accessed using the following syntax:

- **Setting a variable**:
    ```sh
    VAR=value
    ```

- **Using a variable**:
    ```sh
    echo $VAR
    ```

## Contributing

Contributions to this project are welcome! Please feel free to fork the repository, make changes, and submit a pull request.

