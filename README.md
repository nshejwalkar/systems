Neel Shejwalkar(nps92) and Joseph George(jag649)

Custom Shell Implementation: Created custom shell implementation in C that mimics some behaviors of standard Unix shells. It includes parsing user input into commands and arguments, handling redirections (<, >) and pipes (|), and executing both built-in commands (cd, pwd, which, exit) and external programs.

Command Execution Workflow: The shell processes commands by first tokenizing the input string, then creating job structures for each command, which include details like the executable path, arguments, and I/O redirections. For piped commands, it sets up a pipeline, managing inter-process communication and executing each command in a separate child process.

Built-in Functionality: It defines custom functions for built-in commands (my_cd, my_pwd, my_which, my_exit) that are executed directly within the shell process rather than as separate executables. This approach allows the shell to handle commands that affect the shell process state or provide internal information.

I/O Redirection and Piping Mechanism: The shell supports input and output redirection by manipulating file descriptors and uses Unix pipes to connect the stdout of one command to the stdin of another. This functionality is demonstrated in the execution flow for both single and multiple commands, managing file descriptors accordingly to facilitate the correct flow of data between processes.


how to use:

to compile:
    make

to run in interactive mode:
    ./mysh

to run in batch mode:
    ./mysh [Filename]

1. Test plan

Wildcard expansion verification:

Command:
echo test*/test

Expected OUTPUT:
test*/test

2. Conditional execution after a failed command:

Command:
ls arg
else ls

Expected OUTPUT:
ls: cannot access 'arg': No such file or directory
script.txt Makefile mysh mysh.c README.md testingfolder test.c testIn testOut

3. Basic batch mode operation:

Command:
./mysh script.txt

Expected OUTPUT:
script.txt Makefile mysh mysh.c README.md testingfolder test.c testIn testOut
Testing!

4. Conditional execution after a successful command:

Command:
ls
then ls

Expected OUTPUT:
script.txt Makefile mysh mysh.c README.md testingfolder test.c testIn testOut
script.txt Makefile mysh mysh.c README.md testingfolder test.c testIn testOut

5. Conditional execution with 'then' after a failed command:

Command:
ls arg
then ls

Expected OUTPUT:
ls: cannot access 'arg': No such file or directory
[blank]

6. Check for directory change and display path:

Command:
pwd
cd testingfolder
pwd
cd ..
pwd

Expected OUTPUT:
/Users/joeygeorge/Desktop/p3
/Users/joeygeorge/Desktop/p3/testingfolder
/Users/joeygeorge/Desktop/p3

7. Piped commands execution:

Command:
ls | grep test

Expected OUTPUT:
testingfolder
test.c
testInput
testOutput

8. Interactive mode basic operation:

Command:
ls

Expected OUTPUT:
script.txt Makefile mysh mysh.c README.md testingfolder test.c testIn testOut

9. Command 'which' functionality check:

Command:
which ls

Expected OUTPUT:
/usr/bin/ls

10. Piped commands with input redirection:

Command:
ls | wc < testInput

Expected OUTPUT:
4 5 28

11. Input redirection functionality:

Command:
grep test < testIn

Expected OUTPUT:
beach

12. Conditional execution without any conditions after a successful command:

Command:
ls
else ls

Expected OUTPUT:
script.txt Makefile mysh mysh.c README.md testingfolder test.c testIn testOut
[blank]