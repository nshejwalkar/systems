#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <glob.h>

#define DEBUG 1

#define BUFFERSIZE 128
#define INIT_MAXTKNSIZE 128
#define INIT_MAXCMDSIZE 128
#define INIT_MAXCMDARR_SIZE 128
#define INIT_ARGLIST_SIZE 128

#define PREV_SUCCESS 0
#define PREV_FAILURE 1

int last_command_return_value = PREV_SUCCESS;
 char* searchdirectories[3] = {"/usr/local/bin", "/usr/bin", "/bin"};
//char* searchdirectories[3] = {"./test", "./test/test2", "./test/test2/test3"};


typedef enum Conditional {
   THEN,
   ELSE,
   NONE
} Conditional;

typedef struct TknListInfo {
   char** tknlist;
   int length;
} TknListInfo;

typedef struct Job {
   char* executable_path;
   int argc;
   char** arglist;
   char* redirected_input_fd;
   char* redirected_output_fd;
   Conditional conditional;
} Job;

typedef struct PipelineInfo {
   Job* job_pipeline;
   int piped;
} PipelineInfo;

/// CUSTOM COMMANDS
int my_cd(Job job) {
   if (job.argc < 2) {
      printf("cd: not enough arguments\n");
      return -1;
   }
   if (job.argc > 2) {
      printf("cd: too many arguments");
      return -1;
   }
   int status = chdir(job.arglist[1]);
   if (status < 0) {
      printf("error in cd: no directory named %s\n", job.arglist[1]);
      return -1;
   }
   printf("successfully changed directory to %s\n", job.arglist[1]);
   return 0;
}

int my_pwd(Job job) {
   if (job.argc > 1) {
      printf("pwd: too many arguments\n");
      return -1;
   }
   char buffer[PATH_MAX];
   getcwd(buffer, PATH_MAX);
   printf("Current working directory is %s\n", buffer);
   return 0;
}

int my_which(Job job) {
   if (job.argc > 2) {
      printf("which: too many arguments\n");
      return -1;
   }
   if (!strcmp(job.arglist[1], "cd") || !strcmp(job.arglist[1], "pwd") || !strcmp(job.arglist[1], "which") || !strcmp(job.arglist[1], "exit")) {
      printf("which: cannot find path to built-in command\n");
      return -1;
   }
   // now search for it
   for (int i = 0; i<3; i++) {
      // build out full path
      char full_path[strlen(searchdirectories[i]) + 1 + strlen(job.arglist[1]) + 1];
      strcpy(full_path, searchdirectories[i]);
      strcat(full_path, "/");
      strcat(full_path, job.arglist[1]);

      if (access(full_path, F_OK) == 0) {
         printf("which: found executable at %s\n", full_path);
         return 0;
      }
   }
   printf("which: could not find executable for %s\n", job.arglist[1]);
   return -1;
   
}

int my_exit(Job job) {
   printf("exiting the shell: ");
   for (int i = 1; i<job.argc; i++) {
      printf("%s ", job.arglist[i]);
   }
   printf("\n");
   return 0;
}

// frees the commandarr after the program
void free_list(char** list, int num_of_things) {
   for (int i = 0; i<num_of_things; i++) {
      printf("freeing %s\n", list[i]);
      free(list[i]);
   }
   free(list);
}

void print_commands(char** commands, int num_of_commands) {
   for (int i = 0; i<num_of_commands; i++) {
      printf("%s\n", commands[i]);
   }
}

void print_token_list(char** token_list, int num_of_tokens) {
   printf("Printing tokens:\n");
   for (int i = 0; i<num_of_tokens; i++) {
      printf("%s\n", token_list[i]);
   }
}

void print_job(Job job) {
   printf("executable_path: %s\n", job.executable_path);
   printf("argc: %d\n", job.argc);
   printf("arglist: "); print_commands(job.arglist, job.argc);
   printf("redirected_input_fd: %s\n", job.redirected_input_fd);
   printf("redirected_output_fd: %s\n", job.redirected_output_fd);
   printf("conditional: %d\n", job.conditional);
}

void print_pipeline(PipelineInfo pli) {
   if (pli.piped == -1) {printf("Pipeline was cleared because of faulty input.\n"); return;}
   printf("--- JOB 1 ---\n");
   print_job(pli.job_pipeline[0]);
   if (pli.piped) {
      printf("--- JOB 2 ---\n");
      print_job(pli.job_pipeline[1]);
   }
}

void free_job(Job job) {
   if (job.executable_path) {free(job.executable_path);}
   if (job.arglist) {free_list(job.arglist, job.argc);}
   if (job.redirected_input_fd) {free(job.redirected_input_fd);}
   if (job.redirected_output_fd) {free(job.redirected_output_fd);}
}

void free_pipeline(PipelineInfo pli) {
   if (pli.piped == -1) {printf("Pipeline was already cleared because of faulty input.\n"); free(pli.job_pipeline); return;}
   printf("--- FREEING JOB 1 ---\n");
   free_job(pli.job_pipeline[0]);
   if (pli.piped) {
      printf("--- FREEING JOB 2 ---\n");
      free_job(pli.job_pipeline[1]); 
   }
   free(pli.job_pipeline);
}

// void expand_glob(Job* jobaddr, int* argscapacity, char* token) {
//    // expand glob and dump everything into jobaddr->arglist
//    glob_t glob_info;
//    int status = glob(token, GLOB_NOCHECK, NULL, &glob_info);
//    if (status == 0) {
//       // resize argslist array if necessary
//       for (int i = 0; i<glob_info.gl_pathc; i++) {
//          // jobaddr->arglist[jobaddr->argc] = glob_info.gl_pathv[i];
//          add_to_arg_list(jobaddr, argscapacity, glob_info.gl_pathv[i]);
//       }
//    }

//    globfree(&glob_info);
// }
int contains_wildcard(char* string) {
   int i = 0;
   while (string[i]) {
      if (string[i] == '*') {
         return 1;
      }
      i++;
   }
   return 0;
}

int contains_slash(char* string) {
   int i = 0;
   while (string[i]) {
      if (string[i] == '/') {
         return 1;
      }
      i++;
   }
   return 0;
}


int set_executable(Job* job) {
   char* cmd = job->arglist[0];
   // path to file
   if (contains_slash(cmd)) { 
      struct stat statinfo;
      if (stat(cmd, &statinfo) == -1 || !S_ISREG(statinfo.st_mode)) {
         printf("stat err. st_mode is %u", statinfo.st_mode);
         return -1;
      } 
      job->executable_path = (char*)malloc(strlen(cmd)+1);
      strcpy(job->executable_path, cmd);
      return 0;
   }
   // built in command
   else if (!strcmp(cmd, "cd") || !strcmp(cmd, "pwd") || !strcmp(cmd, "which") || !strcmp(cmd, "exit")) {
      printf("executable path is %s\n", cmd);
      job->executable_path = (char*)malloc(strlen(cmd)+1);
      strcpy(job->executable_path, cmd);
      return 0;
   }
   // search for program in specified dirs
   else {
      for (int i = 0; i<3; i++) {
         // build out full path
         char full_path[strlen(searchdirectories[i]) + 1 + strlen(cmd) + 1];  // extra 1 for "/"
         strcpy(full_path, searchdirectories[i]);
         strcat(full_path, "/");
         strcat(full_path, cmd);
         printf("checking file path %s\n", full_path);

         if (access(full_path, F_OK) == 0) {
            printf("found executable at %s\n", full_path);
            job->executable_path = (char*)malloc(strlen(full_path)+1);
            strcpy(job->executable_path, full_path);
            return 0;
         }
      }

   }
   printf("could not find executable %s\n", cmd);
   return -1;
}

int execute_two_jobs(PipelineInfo pli) {  // this executes two jobs in a pipe
   // fd[0] - read
   // fd[1] - write
   int fd[2];
   if (pipe(fd) < 0) {
      printf("pipe error\n");
      last_command_return_value = PREV_FAILURE;
      return -1;
   };
   Job job1 = pli.job_pipeline[0];
   Job job2 = pli.job_pipeline[1];
   int job1_outputset = 0;
   int job2_outputset = 0;

   printf("Spawning child for job 1\n");
   pid_t pid1 = fork();
   if (pid1 < 0) {printf("fork failure\n"); exit(1);}
   else if (pid1 == 0) {  // child process
      close(fd[0]);  // immediately close the read fd, we are not reading from it in the first child
      // check redirection for job1
      if (job1.redirected_input_fd != NULL) {
         // try to open the file
         int fd = open(job1.redirected_input_fd, O_RDONLY);
         if (fd < 0) {
            printf("Input redirection either was not a file or the file could not be found\n"); 
            last_command_return_value = PREV_FAILURE;
            exit(1);
         }
         // if successful, dup2 the fd into stdin
         dup2(fd, STDIN_FILENO);
         close(fd); 
      }
      if (job1.redirected_output_fd != NULL) {
         // try to open the file
         int fd = open(job1.redirected_output_fd, O_WRONLY|O_TRUNC|O_CREAT, 0777);
         if (fd < 0) {
            printf("Output redirection either was not a file or the file could not be found\n"); 
            last_command_return_value = PREV_FAILURE;
            exit(1);
         }
         // if successful, dup2 the fd into stdout
         dup2(fd, STDOUT_FILENO);
         job1_outputset = 1;
         close(fd); 
      }
      // if not specified, write to pipe
      if (!job1_outputset) {
         dup2(fd[1], STDOUT_FILENO);
         close(fd[1]);
      }

      // then try to execute local commands (in the same process)
      if (!strcmp(job1.executable_path, "cd")) {
         printf("running cd\n");
         if (my_cd(job1) < 0) {exit(1);};
         exit(0);
      }
      else if (!strcmp(job1.executable_path, "pwd")) {
         printf("running pwd\n");
         if (my_pwd(job1) < 0) {exit(1);};
         exit(0);
      }
      else if (!strcmp(job1.executable_path, "which")) {
         printf("running which\n");
         if (my_which(job1) < 0) {exit(1);};
         exit(0);
      }
      else if (!strcmp(job1.executable_path, "exit")) {
         printf("running exit\n");
         my_exit(job1);
         exit(0);
      }

      printf("cmd was not built-in, executing from elsewhere\n");
      if (execv(job1.executable_path, job1.arglist) < 0) {
         printf("execv failed for command %s\n", job1.executable_path);
         exit(PREV_FAILURE);  // need to exit() as its a separate process
      }
      exit(PREV_SUCCESS);

   }

   // now almost the same exact thing for job 2
   printf("Spawning child for job 2\n");
   pid_t pid2 = fork();
   if (pid2 < 0) {printf("fork failure\n"); exit(1);}
   else if (pid2 == 0) {  // child process
      close(fd[1]);  // immediately close the write fd, we are not writing to it in the second child
      // check redirection for job2
      if (job2.redirected_input_fd != NULL) {
         // try to open the file
         int fd = open(job2.redirected_input_fd, O_RDONLY);
         if (fd < 0) {
            printf("Input redirection either was not a file or the file could not be found\n"); 
            last_command_return_value = PREV_FAILURE;
            exit(1);
         }
         // if successful, dup2 the fd into stdin
         dup2(fd, STDIN_FILENO);
         job2_outputset = 1;
         close(fd); 
      }
      if (job2.redirected_output_fd != NULL) {
         // try to open the file
         int fd = open(job2.redirected_output_fd, O_WRONLY|O_TRUNC|O_CREAT, 0777);
         if (fd < 0) {
            printf("Output redirection either was not a file or the file could not be found\n"); 
            last_command_return_value = PREV_FAILURE;
            exit(1);
         }
         // if successful, dup2 the fd into stdout
         dup2(fd, STDOUT_FILENO);
         close(fd); 
      }
      // if not specified, read from pipe
      if (!job2_outputset) {
         dup2(fd[0], STDIN_FILENO);
         close(fd[0]);
      }

      // then try to execute local commands (in the same process)
      if (!strcmp(job2.executable_path, "cd")) {
         printf("running cd\n");
         if (my_cd(job2) < 0) {exit(1);};
         exit(0);
      }
      else if (!strcmp(job2.executable_path, "pwd")) {
         printf("running pwd\n");
         if (my_pwd(job2) < 0) {exit(1);};
         exit(0);
      }
      else if (!strcmp(job2.executable_path, "which")) {
         printf("running which\n");
         if (my_which(job2) < 0) {exit(1);};
         exit(0);
      }
      else if (!strcmp(job2.executable_path, "exit")) {
         printf("running exit\n");
         my_exit(job2);
         exit(0);
      }

      printf("cmd was not built-in, executing from elsewhere\n");
      if (execv(job2.executable_path, job2.arglist) < 0) {
         printf("execv failed for command %s\n", job2.executable_path);
         exit(PREV_FAILURE);  // need to exit() as its a separate process
      }
      exit(PREV_SUCCESS);
   }
   // parent process
   close(fd[0]); close(fd[1]);
   int status1;
   int status2;
   waitpid(pid1, &status1, -1);
   waitpid(pid2, &status2, -1);

   if (WEXITSTATUS(status1) == EXIT_SUCCESS && WEXITSTATUS(status2) == EXIT_SUCCESS) {
      last_command_return_value = PREV_SUCCESS;
      return 0;
   }
   else {
      last_command_return_value = PREV_FAILURE;
      return -1;
   }
   return 0;
}

int execute_one_job(Job job) {  // this just executes a single job
   // first check conditional
   if (job.conditional == THEN && last_command_return_value == PREV_FAILURE) {
      printf("Conditional was THEN but last command returned PREV_FAILURE\n");
      last_command_return_value = 0;
      return -1;
   }
   else if (job.conditional == ELSE && last_command_return_value == PREV_SUCCESS) {
      printf("Conditional was ELSE but last command returned PREV_SUCCESS\n");   
      last_command_return_value = 0;
      return -1;
   }

   if (!strcmp(job.executable_path, "cd")) {  // should happen outside of any child processes in order for the output to remain
      printf("running cd\n");
      if (my_cd(job) < 0) {return -1;};
      return -1;
   }

   // then fork and execv
   printf("Spawning child\n");
   pid_t pid = fork();

   if (pid < 0) {printf("fork failure\n"); exit(1);}
   else if (pid == 0) {  // child process
      // first check for any redirections -- these will only happen inside the child process, so stdin/stdout is unchanged for parent
      if (job.redirected_input_fd != NULL) {
         // try to open the file
         int fd = open(job.redirected_input_fd, O_RDONLY);
         if (fd < 0) {
            printf("Input redirection either was not a file or the file could not be found\n"); 
            last_command_return_value = PREV_FAILURE;
            exit(1);
         }
         // if successful, dup2 the fd into stdin
         dup2(fd, STDIN_FILENO);
         close(fd); 
      } 

      if (job.redirected_output_fd != NULL) {
         // try to open the file
         int fd = open(job.redirected_output_fd, O_WRONLY|O_TRUNC|O_CREAT, 0777);
         if (fd < 0) {
            printf("Output redirection either was not a file or the file could not be found\n"); 
            last_command_return_value = PREV_FAILURE;
            exit(1);
         }
         // if successful, dup2 the fd into stdout
         dup2(fd, STDOUT_FILENO);
         close(fd); 
      } 

      // then try to execute local commands (in the same process)
      if (!strcmp(job.executable_path, "pwd")) {
         printf("running pwd\n");
         if (my_pwd(job) < 0) {exit(1);};
         exit(0);
      }
      else if (!strcmp(job.executable_path, "which")) {
         printf("running which\n");
         if (my_which(job) < 0) {exit(1);};
         exit(0);
      }
      else if (!strcmp(job.executable_path, "exit")) {
         printf("running exit\n");
         my_exit(job);
         exit(99);  // special code to give to pass to the parent to immediately quit as well
      }

      printf("cmd was not built-in, executing from elsewhere\n");
      if (execv(job.executable_path, job.arglist) < 0) {
         printf("execv failed for command %s\n", job.executable_path);
         exit(PREV_FAILURE);  // need to exit() as its a separate process
      }
      exit(PREV_SUCCESS);
   }
   else {  // parent process
      printf("in parent\n");
      int status;
      wait(&status);
      printf("in parent\n");
      // set lcrv based on how the child did
      if (WEXITSTATUS(status) == PREV_FAILURE) {last_command_return_value = PREV_FAILURE;}
      else {last_command_return_value = PREV_SUCCESS;}
      if (WEXITSTATUS(status) == 99) {exit(0);}
   }

   return 0;

}

void execute_job_pipeline(PipelineInfo pli) {
   if (pli.piped == 0) {
      execute_one_job(pli.job_pipeline[0]);
      return;
   }
   execute_two_jobs(pli);
}

Job initialize_job() {
   Job job;
   job.executable_path = NULL;
   job.argc = 0;
   job.arglist = NULL;
   job.redirected_input_fd = NULL;
   job.redirected_output_fd = NULL;
   job.conditional = NONE;
   return job;
}

void add_to_arg_list(Job* jobaddr, int* argscapacity, char* token) {
   // resize argslist array if necessary
   if (jobaddr->argc >= *argscapacity) {
      *argscapacity *= 2;

      char** new_arglist = (char**)malloc((*argscapacity) * sizeof(char*));
      for (int i = 0; i < jobaddr->argc; i++) {
         new_arglist[i] = jobaddr->arglist[i];
      }

      free(jobaddr->arglist);
      jobaddr->arglist = new_arglist;
   }
   jobaddr->arglist[jobaddr->argc] = (char *)malloc(strlen(token)+1);
   strcpy(jobaddr->arglist[jobaddr->argc], token);

   (jobaddr->argc)++;
}

PipelineInfo create_job(TknListInfo tknlistinfo) {
   PipelineInfo pipelineinfo;
   // initialize job
   pipelineinfo.job_pipeline = (Job*)malloc(2*sizeof(Job));
   Job current_job = initialize_job();
   current_job.argc = 0;
   int curr_args_capacity = INIT_ARGLIST_SIZE;
   current_job.arglist = (char **)malloc(curr_args_capacity*sizeof(char *));
   Conditional cond = NONE;

   // print_job(current_job);

   int tknslen = tknlistinfo.length;
   char** tkns = tknlistinfo.tknlist;
   int failure = 0;
   int piped = 0;

   // printf("printing tkns\n");
   // print_commands(tkns, tknslen);

   // iterate through tknlistinfo
   for (int i = 0; i<tknslen; i++) {
      printf("tkns[i] is %s\n", tkns[i]);
      // check for conditional
      if (!strcmp(tkns[i], "then") && i==0) {
         if (piped) {printf("can't have THEN in pipe\n"); failure = 1; break;}
         current_job.conditional = THEN;
         cond = THEN;
         continue;
      }
      if (!strcmp(tkns[i], "else") && i==0) {
         if (piped) {printf("can't have ELSE in pipe\n"); failure = 1; break;}
         current_job.conditional = ELSE;
         cond = ELSE;
         continue;
      }

      // check for input redirection
      if (!strcmp(tkns[i], "<")) {
         if (i == tknslen-1 || !strcmp(tkns[i+1], "|")) {
            printf("cannot have < as last token\n");
            failure = 1;
            break;
         }
         current_job.redirected_input_fd = (char *)malloc(strlen(tkns[i+1])+1);
         strcpy(current_job.redirected_input_fd, tkns[i+1]);
         
         i++; continue;
      }
      // check for output redirection
      if (!strcmp(tkns[i], ">")) {
         if (i == tknslen-1 || !strcmp(tkns[i+1], "|")) {
            printf("cannot have > as last token\n");
            failure = 1;
            break;
         }
         current_job.redirected_output_fd = (char *)malloc(strlen(tkns[i+1])+1);
         strcpy(current_job.redirected_output_fd, tkns[i+1]);
         i++; continue;
      }
      // if string contains a wildcard, expand the glob
      // if (contains_wildcard(tkns[i])) {
      //    expand_glob(&current_job, &curr_args_capacity, tkns[i]);
      // }
      // check for pipelines
      if (!strcmp(tkns[i], "|")) {
         if (piped) {
            printf("Can't have more than two piped programs");
            failure = 1;
            break;
         }
         // add current job to pipeline
         pipelineinfo.job_pipeline[0] = current_job;
         if (set_executable(&pipelineinfo.job_pipeline[0]) == -1) {
            printf("set executable failed\n");
            failure = 1;
            break;
         }
         piped = 1;

         // create new job and continue
         current_job = initialize_job();
         current_job.argc = 0;
         current_job.conditional = cond;
         int curr_args_capacity = INIT_ARGLIST_SIZE;
         current_job.arglist = (char **)malloc(curr_args_capacity*sizeof(char *));
         continue;
      }
      // if not any of those things, add to argument list
      printf("adding %s to args list\n", tkns[i]);
      add_to_arg_list(&current_job, &curr_args_capacity, tkns[i]);

   }

   // print_job(current_job);
   // printf("-------\n");
   if (!piped) {
      pipelineinfo.job_pipeline[0] = current_job;
      if (set_executable(&pipelineinfo.job_pipeline[0]) == -1) {printf("set executable failed\n"); failure = 1;}
   }
   if (piped) {
      pipelineinfo.job_pipeline[1] = current_job;
      if (set_executable(&pipelineinfo.job_pipeline[1]) == -1) {printf("set executable failed for 2nd job\n"); failure = 1;}
   }
   pipelineinfo.piped = piped;

   // struct to hold pipeline + whether its piped
   if (failure) {
      printf("Failed, deleting jobs\n");
      free_job(current_job);
      if (piped) {free_job(pipelineinfo.job_pipeline[0]);}
      pipelineinfo.piped = -1;  // special error
      free_list(tknlistinfo.tknlist, tknlistinfo.length);
      return pipelineinfo;

   }
   

   // print_job(pipelineinfo.job_pipeline[0]);
   printf("Job creation was a success, freeing temporary tknlist\n");
   free_list(tknlistinfo.tknlist, tknlistinfo.length);

   return pipelineinfo;
}

// turns the string into an list of tokens to later turn into a job
TknListInfo parse_command(char* cmd_string) {
   TknListInfo tknlistinfo;
   char** command_arg_list = NULL;
   int num_of_tokens = 0;

   char* current_token = (char*)malloc(INIT_MAXTKNSIZE*sizeof(char));
   int token_size = 0;
   int inside_token = 0;

   int index = 0;

   // dynamically creates a list of tokens for the command
   while ((cmd_string[index]) != '\0') {
      // printf("%c ", cmd_string[index]);
      // printf("%d ", index);
      if (cmd_string[index] == '<' || cmd_string[index] == '>' || cmd_string[index] == '|') {
         if (inside_token == 1) {  // add the current token to the list
            printf("inside token\n");
            // create pointer to token
            char* new_token = (char *)malloc((token_size+1)*sizeof(char));
            for (int j = 0; j<token_size; j++) {
               new_token[j] = current_token[j];
            }
            new_token[token_size] = '\0';

            // add the new token in
            command_arg_list = realloc(command_arg_list, (num_of_tokens+1)*sizeof(char *));
            command_arg_list[num_of_tokens] = new_token;
            num_of_tokens++;
         }
         // then add the <>|

         // create pointer to token
         char* new_token = (char *)malloc(2*sizeof(char));
         new_token[0] = cmd_string[index];
         new_token[1] = '\0';
         
         // add the new token in
         command_arg_list = realloc(command_arg_list, (num_of_tokens+1)*sizeof(char *));
         // printf("a\n");
         command_arg_list[num_of_tokens] = new_token;
         // printf("b\n");
         num_of_tokens++;
         // printf("c\n");

         inside_token = 0;
         token_size = 0;
      }
      else if (cmd_string[index] == ' ') {
         if (inside_token == 0) {index++; continue;}  // multiple spaces/empty word

         // create pointer to token
         char* new_token = (char *)malloc((token_size+1)*sizeof(char));
         for (int j = 0; j<token_size; j++) {
            new_token[j] = current_token[j];
         }
         new_token[token_size] = '\0';

         // add the new token in
         command_arg_list = realloc(command_arg_list, (num_of_tokens+1)*sizeof(char *));
         command_arg_list[num_of_tokens] = new_token;
         num_of_tokens++;
         
         inside_token = 0;
         token_size = 0;
      }
      else {  // we come across actual letters
         current_token[token_size] = cmd_string[index];
         token_size++;

         if (!inside_token) {
            inside_token = 1;
         }
         
      }

      index++;
   }
   // if the last token doesn't end in a space
   if (inside_token) {
      // create pointer to token
      char* new_token = (char *)malloc((token_size+1)*sizeof(char));
      for (int j = 0; j<token_size; j++) {
         new_token[j] = current_token[j];
      }
      new_token[token_size] = '\0';

      // add the new token in
      command_arg_list = realloc(command_arg_list, (num_of_tokens+1)*sizeof(char *));
      command_arg_list[num_of_tokens] = new_token;
      num_of_tokens++;
   }

   free(current_token);

   tknlistinfo.tknlist = command_arg_list;
   tknlistinfo.length = num_of_tokens;

   return tknlistinfo;
}

// read single command (called once by interactive, multiple times by batch)
// read all commands (called by batch, calls read single command within it) builds 

char* read_single_command(int input_fd) {
   int bufferSize = BUFFERSIZE;
   int index = 0;
   char* command = (char*)malloc(bufferSize * sizeof(char));

   while (1) {
      char ch;
      int numRead = read(input_fd, &ch, 1);
      if (numRead < 0) {
         perror("read");
         free(command);
         exit(EXIT_FAILURE);
      } 
      // reached the EOF
      else if (numRead == 0) { 
         if (index == 0) {
            free(command);
            return NULL;
         }
         break; // Some characters read before EOF
      } 
      // normal letter
      else {
         if (ch == '\n') {
            break; // End of command
         }
         command[index] = ch;
         index++;
         if (index >= bufferSize) {
            bufferSize *= 2;
            command = (char*)realloc(command, bufferSize);
         }
      }
   }
   command[index] = '\0';
   // printf("Command is %s\n", command);

   return command;
}


void run_batch_mode(char* path) {
   int fd;

   if (path == NULL) {  // piped input
      fd = STDIN_FILENO;
   }
   else {  // cmd arg file
      struct stat file_stat;

      int status = stat(path, &file_stat);
      if (status < 0) {perror("Couldn't open file"); exit(1);}

      if (!S_ISREG(file_stat.st_mode)) {
         printf("argument is not a file\n"); exit(1);
      }

      fd = open(path, O_RDONLY);
      if (fd < 0) {
         perror("Couldn't open file"); exit(1);
      }
   }
   
   char** commands = NULL;
   int num_of_cmds = 0;
   char* current_command;
   PipelineInfo pli;

   // dynamically creates a list of commands to execute
   while ((current_command = read_single_command(fd)) != NULL) {
      commands = realloc(commands, (num_of_cmds+1)*sizeof(char *));
      commands[num_of_cmds] = current_command;
      num_of_cmds++;
   }

   // print_commands(commands, num_of_cmds);

   // execute each command
   for (int i = 0; i<num_of_cmds; i++) {
      printf("printing tokens for %s\n", commands[i]);
      TknListInfo cmdlistinfo = parse_command(commands[i]);
      print_token_list(cmdlistinfo.tknlist, cmdlistinfo.length);
      pli = create_job(cmdlistinfo);
      print_pipeline(pli);

      // free_list(cmdlistinfo.tknlist, cmdlistinfo.length);
      free_pipeline(pli);
   }

   free_list(commands, num_of_cmds);

   close(fd);

   
   return;
}

void run_interactive_mode() {
   char* cmd;
   PipelineInfo pli;

   printf("Welcome to my shell!\nmysh> ");
   fflush(stdout);

   while (1) {
      cmd = read_single_command(STDIN_FILENO);
      TknListInfo cmdlistinfo = parse_command(cmd);
      print_token_list(cmdlistinfo.tknlist, cmdlistinfo.length);
      pli = create_job(cmdlistinfo);
      print_pipeline(pli);
      if (pli.piped != -1) {
         execute_job_pipeline(pli);
      }
      free_pipeline(pli);

      // printf("cmd was %s\nmysh> ", cmd);
      printf("mysh> ");
      fflush(stdout);

      free(cmd);
   }
   
   printf("Exiting the shell!");

   return;
}

int main(int argc, char** argv) {
   int input_tty = isatty(STDIN_FILENO);
   int output_tty = isatty(STDOUT_FILENO);

   printf("%d, %d\n", input_tty, output_tty);

   // TknListInfo cmdlistinfo = parse_command("hello my |name>k   is");
   // print_token_list(cmdlistinfo.tknlist, cmdlistinfo.length);

   if (argc == 2) {
      if (DEBUG) {
         printf("File specified in args. Running batch mode\n");
      }
      run_batch_mode(argv[1]);
   }
   else if (input_tty == 0 && output_tty == 1) {
      if (DEBUG) {
         printf("Piped input. Running batch mode\n");
      }
      run_batch_mode((char *)NULL);
   }
   else if (input_tty == 1 && output_tty == 1) {
      if (DEBUG) {
         printf("No file/pipe. Running interactive mode\n");
      }
      run_interactive_mode();
   }

   return 0;
}