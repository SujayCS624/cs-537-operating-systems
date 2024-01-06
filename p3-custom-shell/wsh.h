#include<sys/types.h>

struct Job {
    pid_t pid;
    int jobID;
    char *arguments[1024];
    int isValid;
    int isBackground;
    int hasAmpersand;
    int isStopped;
};

void initializeJobIDs();
void initializeJob(struct Job *job);
void updateBackgroundJobStatus();
int findSmallestAvailableJobID();
int findLargestUsedJobID();
int storeJobInfo(char *arguments[], int processID, int isBackgroundJob, int hasAmpersand, int isStopped);
int parseLine(char* commandList[], char* inputLine);
int parseCommand(char *arguments[], char* command);
int execExitCommand(char *arguments[]);
int execCdCommand(char *arguments[]);
int execJobsCommand(char *arguments[]);
int execFgCommand(char *arguments[]);
int execBgCommand(char *arguments[]);
int execExternalJob(char *commands[], int numCommands);
int execExternalCommand(char *arguments[], int numArguments);
int runInteractiveMode();
int runBatchMode(char *batchFile);