#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

// Helper function to search for the keyword in a file
int fileSearch(char* filePath, char* keyword, char* section, char* fileName) {
    // Open file to be read and handle error case
    FILE *file = fopen(filePath, "r");
    if (file == NULL) {
        printf("cannot open file\n");
        exit(1);
    }
    int isNameSection = 0;
    int isDescriptionSection = 0;
    int isNameLine = 0;
    int keywordFoundFlag = 0;

    char line[1024];
    char name[1024];

    // Iterate over each line in the file
    while (fgets(line, sizeof(line), file) != NULL) {
        // Check if we are entering name section
        if (strcmp(line, "\033[1mNAME\033[0m\n") == 0) {
            isNameSection = 1;
            isNameLine = 1;
            isDescriptionSection = 0;
            continue;
        }
        // Extract name one-liner if on the first line of the name section
        else if (isNameLine == 1) {
            isNameLine = 0;
            strcpy(name, strstr(line, "-") + 1);
        }
        // Check if we are entering description section
        else if (strcmp(line, "\033[1mDESCRIPTION\033[0m\n") == 0) {
            isNameSection = 0;
            isDescriptionSection = 1;
            continue;
        }

        // Check if we are entering a section other than name or description or the end of file
        else if ((isDescriptionSection || isNameSection) && strstr(line, "       ") == NULL) {
            isDescriptionSection = 0;
            isNameSection = 0;
        }

        // Search for keyword in name and description section
        if (isNameSection || isDescriptionSection) {
            if (strstr(line, keyword) != NULL) {
                keywordFoundFlag = 1;
                break;
            }
        }
    }
    fclose(file);

    // If keyword is found, print the filename, section number, and name one-liner
    if(keywordFoundFlag == 1) {
        printf("%s (%s) -%s", fileName, section, name);
        return 0;
    }
    // Keyword not found in file
    return -1;
}

// Helper function to search for the keyword in a specific directory
int sectionSearch(char* section, char* keyword) {
    int keywordFoundFlag = 0;
    // Convert section argument to an integer
    int sectionNumber = atoi(section);

    // Construct path to directory
    char rootDirectory[256] = "./man_pages";
    char directoryToSearch[256];
    char directoryPath[712];
    sprintf(directoryToSearch, "man%d", sectionNumber);
    sprintf(directoryPath, "%s/%s", rootDirectory, directoryToSearch);

    // Open directory and handle error case
    DIR *dir = opendir(directoryPath);
    if (dir == NULL) {
        printf("cannot open directory\n");
        exit(1);
    }

    struct dirent *entry;

    // Iterate over all files in the directory and call fileSearch for each file
    while ((entry = readdir(dir)) != NULL) {
            // Construct path to file
            char filePath[1024];
            snprintf(filePath, sizeof(filePath), "%s/%s", directoryPath, entry->d_name);
            // Extract file name from filename.section
            char fileName[256];
            char fileNameCopy[256];
            strncpy(fileNameCopy, entry->d_name, sizeof(fileNameCopy));
            char *token = strtok(fileNameCopy, ".");
            if (token == NULL) {
                continue;
            }
            strncpy(fileName, token, sizeof(fileName));
            // Call fileSearch helper function
            int res = fileSearch(filePath, keyword, section, fileName);
            if (res == 0) {
                keywordFoundFlag = 1;
            }
            
    }
    closedir(dir);
    if (keywordFoundFlag == 1) {
        return 2;
    }
    else {
        return 0;
    }
}

// Helper function to search for the keyword across all directories
int globalSearch(char* keyword) {
    int keywordFoundFlag = 0;
    // Iterate over all directories within man_pages
    for(int currentSectionNumber = 1; currentSectionNumber <= 9; currentSectionNumber++) {
        char currentSection[5];
        sprintf(currentSection, "%d", currentSectionNumber);
        // Call sectionSearch helper function for searching within a specific directory
        int res = sectionSearch(currentSection, keyword);
        if(res == 2) {
            keywordFoundFlag = 1;
        }
        else if (res == 1) {
            return 1;
        }
    }
    if(keywordFoundFlag == 0) {
        // Keyword not found in any directory of man_pages
        printf("nothing appropriate\n");
    }
    return 0;
}

int main(int argc, char *argv[]) {
    // Argument provided, ignore extra args if any
    if (argc >= 2) {
        int status = globalSearch(argv[1]);
        return status;
    }
    // Handle cases with invalid number of arguments provided
    else if (argc == 1) {
        printf("wapropos what?\n");
        return 0;
    }
    return 0;
}
