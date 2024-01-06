#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<signal.h>

// Global variables to store shell process group and current foreground process group
pid_t currentForegroundProcessGroup = 0; 
pid_t shellProcessGroup = 0;

// Structure for storing information about jobs
struct Job {
    pid_t pid;
    int jobID;
    char *arguments[1024];
    int isValid;
    int isBackground;
    int hasAmpersand;
    int isStopped;
};

// Mapping from job ID to index where that job is located in Jobs
int jobIDs[256];

struct Job Jobs[256];
int numJobs = 0;

// Signal handler for Ctrl-C (SIGINT)
void sigint_handler(int signum) {
    // If current foreground process group isn't the shell process group
    if(getpgid(0) != shellProcessGroup) {
        // Send SIGINT to the foreground process group & give control to the shell process group
        if(currentForegroundProcessGroup > 0) {
            kill(currentForegroundProcessGroup * -1, SIGINT);
            currentForegroundProcessGroup = shellProcessGroup;
            tcsetpgrp(STDIN_FILENO, shellProcessGroup);
        }
    }
}

// Signal handler for Ctrl-Z (SIGTSTP)
void sigtstp_handler(int signum) {
    // If current foreground process group isn't the shell process group
    if(getpgid(0) != shellProcessGroup) {
        // Send SIGTSTP to the foreground process group & give control to the shell process group
        if(currentForegroundProcessGroup > 0) {
            kill(currentForegroundProcessGroup * -1, SIGTSTP);
            currentForegroundProcessGroup = shellProcessGroup;
            tcsetpgrp(STDIN_FILENO, shellProcessGroup);
        }
    }
}

// Initialize job IDs with default values
void initializeJobIDs() {
    for(int i=0; i < 256; i++) {
        jobIDs[i] = -1;
    }
}

// Initialize a job with default values
void initializeJob(struct Job *job) {
    job->pid = -1;
    job->jobID = -1;
    for (int i = 0; i < 1024; i++) {
        job->arguments[i] = NULL;
    }
    job->isValid = -1;
    job->isBackground = -1; 
    job->hasAmpersand = -1; 
    job->isStopped = -1;
}

// Update the status of a background job if it is completed
void updateBackgroundJobStatus() {
    for(int i=0; i < 256; i++) {
        // Check if job is valid and in the background
        if(Jobs[i].isValid == 1 && Jobs[i].isBackground == 1) {
            // Job has multiple commands
            if(Jobs[i].pid < 0) {
                // Iterate over each process in the job
                for(int j=0; j < 256; j++) {
                    // If job is still running, this is a no-op
                    int status;
                    // Wait for any process in the job's process group
                    pid_t result = waitpid(Jobs[i].pid, &status, WNOHANG);
                    // No processes remaining in the job's process group so job is completed
                    if (result == -1) {
                        jobIDs[Jobs[i].jobID] = -1;
                        initializeJob(&Jobs[i]);
                        break;
                    } 
                }
            }
            // Job has a single command
            else {
                // If job is still running, this is a no-op
                int status;
                pid_t result = waitpid(Jobs[i].pid, &status, WNOHANG);
                // Error in waitpid
                if (result == -1) {
                    perror("waitpid");
                    exit(-1);
                }
                // Job is completed
                else if (result > 0) {
                    jobIDs[Jobs[i].jobID] = -1;
                    initializeJob(&Jobs[i]);
                } 
            }    
        }
    }
}

// Find smallest available job ID
int findSmallestAvailableJobID() {
    for (int i=1; i < 256; i++) {
        if(jobIDs[i] == -1) {
            return i;
        }
    }
    // If no job ID is available, return -1
    return -1;
}

// Find the largest job ID that is currently in use
int findLargestUsedJobID() {
    // If no jobs are present in the background, return -1
    int largestJobID = -1;
    for (int i=1; i < 256; i++) {
        if(jobIDs[i] != -1) {
            largestJobID = i;
        }
    }
    return largestJobID;
}

// Store job information in the jobs data structure
int storeJobInfo(char *arguments[], int processID, int isBackgroundJob, int hasAmpersand, int isStopped) {
    struct Job job;
    job.pid = processID;
    job.isValid = 1;
    job.isBackground = isBackgroundJob;
    job.hasAmpersand = hasAmpersand;
    job.isStopped = isStopped;
    for (int i = 0; arguments[i] != NULL; i++) {
        job.arguments[i] = strdup(arguments[i]);
    }
    // Assign a job ID only if it is a background job
    if(job.isBackground == 1) {
        job.jobID = findSmallestAvailableJobID();
        jobIDs[job.jobID] = numJobs; 
    }
    // Assign a job ID of 0 if it is a foreground job
    else {
        job.jobID = 0;
    }
    // Store job in Jobs and increment job counter 
    Jobs[numJobs++] = job;
    return 0;
}

// Parse a line of input and extract all the commands present in that line
int parseLine(char* commandList[], char* inputLine) {
    char* token;
    int numCommands = 0;

    while ((token = strsep(&inputLine, "|")) != NULL) {
        if (strlen(token) > 0) {
            commandList[numCommands++] = token;
        }
    }
    
    commandList[numCommands] = NULL;
    return numCommands;
}

// Parse a command and extract all the arguments present in that command
int parseCommand(char *arguments[], char* command) {
    char *token;
    int numArguments = 0;
    
    while ((token = strsep(&command, " ")) != NULL) {
        if (strlen(token) > 0) {
            arguments[numArguments++] = token;
        }
    }
    
    arguments[numArguments] = NULL;
    return numArguments;
}

// Built-in exit command
int execExitCommand(char *arguments[]) {
    if(arguments[1] != NULL) {
        printf("Invalid number of arguments to exit\n");
        exit(-1);
    }
    else {
        exit(0);
    }
}

// Built-in cd command
int execCdCommand(char *arguments[]) {
    if(arguments[1] == NULL || arguments[2] != NULL) {
        printf("Invalid number of arguments to cd\n");
        exit(-1);
    }
    else {
        // chdir() system call failure
        if (chdir(arguments[1]) != 0) {
            perror("chdir");
            exit(-1);
        }
        return 0;
    }
}

// Built-in jobs command
int execJobsCommand(char *arguments[]) {
    if(arguments[1] != NULL) {
        printf("Invalid number of arguments to jobs\n");
        exit(-1);
    }
    else {
        for (int i = 1; i < 256; i++) {
            int jobIndex = jobIDs[i];
            // Check if jobIndex is valid
            if(jobIndex >= 0 && jobIndex <= 255) {
                printf("%d: %s", Jobs[jobIndex].jobID, Jobs[jobIndex].arguments[0]);
                // Print arguments
                for (int j = 1; Jobs[jobIndex].arguments[j] != NULL; j++) {
                    printf(" %s", Jobs[jobIndex].arguments[j]);
                }
                // Append '&' if it was started as a background job
                if (Jobs[jobIndex].hasAmpersand) {
                    printf(" &");
                }
                printf("\n");
            }
        }
    }

    return 0;
}

// Built-in fg command
int execFgCommand(char *arguments[]) {
    int jobID;
    
    if(arguments[2] != NULL) {
        printf("Invalid number of arguments to fg\n");
        exit(-1);
    }
    else if(arguments[1] != NULL) {
        jobID = atoi(arguments[1]);
        if(jobID < 1 || jobID > 255) {
            printf("Invalid job ID provided\n");
            exit(-1);
        }
    }
    else {
        jobID = findLargestUsedJobID();
        if(jobID == -1) {
            printf("No jobs present in the background\n");
            exit(-1);
        }
    }

    int jobIndex = jobIDs[jobID];
    if(jobIndex == -1 || Jobs[jobIndex].isValid == -1) {
        printf("Invalid job index for job ID: %d\n", jobID);
        exit(-1);
    }

    pid_t pid = Jobs[jobIndex].pid;
    // Update the job structure for the job since it is now in the foreground
    Jobs[jobIndex].isBackground = 0;
    jobIDs[jobID] = -1;
    
    // If job is stopped by Ctrl+Z, resume the job
    if(Jobs[jobIndex].isStopped == 1) {
        kill(pid, SIGCONT);
    }
    // Update the stopped status since it is now resumed
    Jobs[jobIndex].isStopped = 0;
    // Job has multiple commands
    if(pid < 0) {
        // Update foreground process group
        tcsetpgrp(STDIN_FILENO, pid * -1);
    }
    // Job has a single command
    else {
        // Update foreground process group
        tcsetpgrp(STDIN_FILENO, pid);
    }
    
    currentForegroundProcessGroup = tcgetpgrp(STDIN_FILENO);
    // Job has multiple commands
    if(pid < 0) {
        // Wait for all processes in the job's process group to complete
        while(1) {
            int currStatus;
            // No processes remaining in the job's process group so job is completed
            if (waitpid(pid, &currStatus, WUNTRACED) == -1) {
                initializeJob(&Jobs[jobIndex]);
                jobIDs[jobID] = -1;
                break;
            }
            // Job stopped using Ctrl+Z
            if (WIFSTOPPED(currStatus)) {
                Jobs[jobIndex].isStopped = 1;
                Jobs[jobIndex].isBackground = 1;
                jobIDs[jobID] = jobIndex;
                break;
            }
        }
    }
    // Job has a single command
    else {
        // Wait for process to complete
        int status;
        if (waitpid(pid, &status, WUNTRACED) == -1) {
            perror("waitpid");
            exit(-1);
        }
        // Job stopped using Ctrl+Z
        if(WIFSTOPPED(status)) {
            Jobs[jobIndex].isStopped = 1;
            Jobs[jobIndex].isBackground = 1;
            jobIDs[jobID] = jobIndex;
        }
        // Job is completed
        else if(WIFEXITED(status)) {
            initializeJob(&Jobs[jobIndex]);
            jobIDs[jobID] = -1;
        }
    }

    // Give control to the shell process group
    tcsetpgrp(STDIN_FILENO, shellProcessGroup);
    currentForegroundProcessGroup = tcgetpgrp(STDIN_FILENO);
    return 0;
}

// Built-in bg command
int execBgCommand(char *arguments[]) {
    int jobID;

    if(arguments[2] != NULL) {
        printf("Invalid number of arguments to bg\n");
        exit(-1);
    }
    else if(arguments[1] != NULL) {
        jobID = atoi(arguments[1]);
        if(jobID < 1 || jobID > 255) {
            printf("Invalid job ID provided\n");
            exit(-1);
        }
    }
    else {
        jobID = findLargestUsedJobID();
        if(jobID == -1) {
            printf("No jobs present in the background\n");
            exit(-1);
        }
    }

    int jobIndex = jobIDs[jobID];
    if(jobIndex == -1 || Jobs[jobIndex].isValid == -1) {
        printf("Invalid job index for job ID: %d\n", jobID);
        exit(-1);
    }

    pid_t pid = Jobs[jobIndex].pid;
    // Update the job structure for the job since it is now in the background
    Jobs[jobIndex].isBackground = 1;
    
    // If job is stopped by Ctrl+Z, resume the job
    if(Jobs[jobIndex].isStopped == 1) {
        kill(pid, SIGCONT);
    }

    // Update the stopped status since it is now resumed
    Jobs[jobIndex].isStopped = 0;
    return 0;
}

// Run an external job containing multiple commands
int execExternalJob(char *commands[], int numCommands) {
    int numPipes = numCommands - 1;
    int pipes[numPipes][2];
    int processIDs[256];
    int isBackgroundJob = 0;
    int isStopped = 0;
    // Parse the final command to check if it is a background job
    char *finalCommand = strdup(commands[numCommands-1]);
    char *finalCommandArgs[256];
    int numArgumentsInFinalCommand = parseCommand(finalCommandArgs, finalCommand);

    if (numArgumentsInFinalCommand == 1 && strcmp(finalCommandArgs[numArgumentsInFinalCommand-1], "&") == 0) {
        printf("No command provided to be run in the background\n");
        exit(-1);
    }
    // Background job
    else if (strcmp(finalCommandArgs[numArgumentsInFinalCommand-1], "&") == 0){
        isBackgroundJob = 1;
    }

    // Store the job arguments in the format required for the jobs command
    char *arguments[1024];
    int totalArguments = 0;

    for(int i = 0; i < numCommands; i++) {
        char *currentCommand = strdup(commands[i]);
        char *currentCommandArgs[256];
        parseCommand(currentCommandArgs, currentCommand);
        for(int j = 0; currentCommandArgs[j] != NULL; j++) {
            arguments[totalArguments++] = strdup(currentCommandArgs[j]);
        }
        if(i != numCommands - 1) {
            arguments[totalArguments++] = "|";
        }
    }
    if (isBackgroundJob == 1) {
        arguments[totalArguments-1] = NULL;
    }

    // Initialize the pipe descriptors        
    for (int i = 0; i < numPipes; i++) {
        if (pipe(pipes[i]) == -1) {
        perror("pipe");
        exit(-1);
        }
    }

    // Iterate over each command in the job
    for (int i = 0; i < numCommands; i++) {
        // Parse command into arguments
        char *inputLineArgs[256];
        int numArguments = parseCommand(inputLineArgs, commands[i]);
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(-1);
        }
        // Child process
        if (pid == 0) {
            // First command
            if (i == 0) {
                // Redirect output to the write end of the first pipe
                dup2(pipes[i][1], STDOUT_FILENO);
                // Set Process Group ID to Process ID of first child process
                setpgid(0, 0);
                // If it isn't a background job, give control to the job
                if(isBackgroundJob == 0) {
                    tcsetpgrp(STDIN_FILENO, getpid());
                    currentForegroundProcessGroup = tcgetpgrp(STDIN_FILENO);
                }
            }
            // Last command 
            else if (i == numCommands - 1) {
                // Redirect input from the read end of the last pipe
                dup2(pipes[i - 1][0], STDIN_FILENO);
                // Remove & if it is a background job
                if(isBackgroundJob == 1) {
                    inputLineArgs[numArguments-1] = NULL;
                }
            } 
            // Any other command except first and last
            else {
                // Redirect both input and output to pipes
                dup2(pipes[i - 1][0], STDIN_FILENO);
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            // Close all pipe descriptors
            for (int j = 0; j < numPipes; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // Execute the command
            if (execvp(inputLineArgs[0], inputLineArgs) == -1) {
                perror("execvp");
                exit(-1);
            }
        }
        // Parent process
        else {
            // Store the Process ID of child process
            processIDs[i] = pid;
            // Set Process Group ID to Process ID of first child process for all processes of the pipeline
            setpgid(pid, processIDs[0]);
        }
    }
    // Close all pipe descriptors
    for (int j = 0; j < numPipes; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
    }
    
    // If it is a foreground job 
    if(isBackgroundJob == 0) {
        // Wait for all the processes of the job to complete
        for (int i = 0; i < numCommands; i++) {
            int status;
            if (waitpid(processIDs[i], &status, WUNTRACED) == -1) {
                perror("waitpid");
                exit(-1);
            }
            // Process stopped using Ctrl+Z
            if(WIFSTOPPED(status)) {
                isStopped = 1;
            }
        }
        if(isStopped == 1) {
            // Store negative of Process Group ID to indicate that this is a job with multiple processes
            storeJobInfo(arguments, processIDs[0] * -1, 1, 0, 1);
        }
        // Restore control to the shell process group
        currentForegroundProcessGroup = shellProcessGroup;
        tcsetpgrp(STDIN_FILENO, shellProcessGroup);
    }
    // If it is a background job
    else {
        // Store negative of Process Group ID to indicate that this is a job with multiple processes
        storeJobInfo(arguments, processIDs[0] * -1, 1, 1, 0);
    }

    return 0;           
}

// Run an external job containing a single command
int execExternalCommand(char *arguments[], int numArguments) {
    int isBackgroundJob = 0;
    if (numArguments == 1 && strcmp(arguments[numArguments-1], "&") == 0) {
        printf("No command provided to be run in the background\n");
        exit(-1);
    }
    // Check if it is a background job
    else if (strcmp(arguments[numArguments-1], "&") == 0){
        isBackgroundJob = 1;
        arguments[numArguments-1] = NULL;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(1);
    }
    // Child process
    else if (pid == 0) {
        // Set Process Group ID to Process ID of child process
        setpgid(0, 0);
        // If it isn't a background job, give control to the job
        if(isBackgroundJob == 0) {
            tcsetpgrp(STDIN_FILENO, getpid());
            currentForegroundProcessGroup = tcgetpgrp(STDIN_FILENO);
        }
        // Execute the command
        if (execvp(arguments[0], arguments) == -1) {
            perror("execvp");
            exit(-1);
        }
    }
    // Parent process
    else {
        // If it is a foreground job
        if(isBackgroundJob == 0) {
            // Wait for the process to complete
            int status;
            if (waitpid(pid, &status, WUNTRACED) == -1) {
                perror("waitpid");
                exit(-1);
            }
            // Process stopped with Ctrl+Z
            if(WIFSTOPPED(status)) {
                storeJobInfo(arguments, pid, 1, 0, 1);
            }
            // Restore control to the shell process group
            currentForegroundProcessGroup = shellProcessGroup;
            tcsetpgrp(STDIN_FILENO, shellProcessGroup);
        }
        // If it is a background job
        else {
            storeJobInfo(arguments, pid, 1, 1, 0);
        }
    }

    return 0;
}

// Run the shell in interactive mode
int runInteractiveMode() {
    // Initialize shell process group and set it to the foreground
    shellProcessGroup = tcgetpgrp(STDIN_FILENO);
    tcsetpgrp(STDIN_FILENO, shellProcessGroup);
    currentForegroundProcessGroup = shellProcessGroup;

    // Initialize signal handlers for Ctrl+C, Ctrl+Z and background process write to terminal
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);
    signal(SIGTTOU, SIG_IGN);
    

    // Run infinite loop
    while(1) {
        printf("wsh> ");
        fflush(stdout);

        // Check and update status of background jobs
        updateBackgroundJobStatus();

        char *inputLine = NULL;
        size_t lineLength = 0;

        // Read line of input until Ctrl+D is encountered
        if (getline(&inputLine, &lineLength, stdin) == -1) {
            if (feof(stdin)) {
                exit(0);
            }
            else {
                perror("getline");
                exit(-1);
            }   
        }

        // Make inputLine null terminated
        inputLine[strcspn(inputLine, "\n")] = '\0';

        // Parse line into list of commands
        char* commandList[256] = { NULL };
        int numCommands = parseLine(commandList, inputLine);

        // If | is present, run an external job containing multiple commands
        if(numCommands > 1) {
            execExternalJob(commandList, numCommands);
        }
        // Single command
        else {
            // Parse command into arguments
            char *inputLineArgs[256] = { NULL };
            int numArguments = parseCommand(inputLineArgs, inputLine);

            // Check if built-in exit command should be called
            if(strcmp(inputLineArgs[0], "exit") == 0) {
                execExitCommand(inputLineArgs);
            }
            // Check if built-in cd command should be called
            else if(strcmp(inputLineArgs[0], "cd") == 0) {
                execCdCommand(inputLineArgs);
            }
            // Check if built-in jobs command should be called
            else if(strcmp(inputLineArgs[0], "jobs") == 0) {
                updateBackgroundJobStatus();
                execJobsCommand(inputLineArgs);
            }
            // Check if built-in fg command should be called
            else if(strcmp(inputLineArgs[0], "fg") == 0) {
                execFgCommand(inputLineArgs);
            }
            // Check if built-in bg command should be called
            else if(strcmp(inputLineArgs[0], "bg") == 0) {
                execBgCommand(inputLineArgs);
            }
            // Execute external command
            else {
                execExternalCommand(inputLineArgs, numArguments);
            }
        }
    }

    return 0;
}

// Run the shell in batch mode
int runBatchMode(char *batchFile) {
    char *inputLine = NULL;
    size_t lineLength = 0;

    // Initialize shell process group and set it to the foreground
    shellProcessGroup = tcgetpgrp(STDIN_FILENO);
    tcsetpgrp(STDIN_FILENO, shellProcessGroup);
    currentForegroundProcessGroup = shellProcessGroup;

    // Initialize signal handlers for Ctrl+C, Ctrl+Z and background process write to terminal
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);
    signal(SIGTTOU, SIG_IGN);
    
    // Open batch file of commands in read mode
    FILE *file;
    file = fopen(batchFile, "r");
    if (file == NULL) {
        perror("Error opening file");
        exit(-1);
    }

    // Iterate over file line by line
    while (getline(&inputLine, &lineLength, file) != -1) {
        // Make inputLine null terminated
        inputLine[strcspn(inputLine, "\n")] = '\0';

        // Check and update status of background jobs
        updateBackgroundJobStatus();

        // Parse line into list of commands
        char* commandList[256] = { NULL };
        int numCommands = parseLine(commandList, inputLine);

        // If | is present, run an external job containing multiple commands
        if(numCommands > 1) {
            execExternalJob(commandList, numCommands);
        }
        // Single command
        else {
            // Parse command into arguments
            char *inputLineArgs[256] = { NULL };
            int numArguments = parseCommand(inputLineArgs, inputLine);

            // Check if built-in exit command should be called
            if(strcmp(inputLineArgs[0], "exit") == 0) {
                execExitCommand(inputLineArgs);
            }
            // Check if built-in cd command should be called
            else if(strcmp(inputLineArgs[0], "cd") == 0) {
                execCdCommand(inputLineArgs);
            }
            // Check if built-in jobs command should be called
            else if(strcmp(inputLineArgs[0], "jobs") == 0) {
                updateBackgroundJobStatus();
                execJobsCommand(inputLineArgs);
            }
            // Check if built-in fg command should be called
            else if(strcmp(inputLineArgs[0], "fg") == 0) {
                execFgCommand(inputLineArgs);
            }
            // Check if built-in bg command should be called
            else if(strcmp(inputLineArgs[0], "bg") == 0) {
                execBgCommand(inputLineArgs);
            }
            // Execute external command
            else {
                execExternalCommand(inputLineArgs, numArguments);
            }
        }
    }
    // Check if Ctrl+D is encountered at end of file
    if (feof(file)) {
        exit(0);
    }
    else {
        perror("getline");
        exit(-1);
    }

    // Close file
    fclose(file);

    return 0;
}

int main(int argc, char *argv[]) {
    int retVal = 0;
    // Initialize data structures to store jobs and job IDs
    for (int i = 0; i < 256; i++) {
        initializeJob(&Jobs[i]);
    }
    initializeJobIDs();

    // If no arguments, run in interactive mode
    if(argc == 1) {
        retVal = runInteractiveMode();
    }
    // If argument provided, run in batch mode
    else if(argc == 2) {
        retVal = runBatchMode(argv[1]);
    }
    // Invalid number of arguments
    else {
        printf("Invalid number of arguments\n");
        retVal = -1;
    }

    return retVal;
}