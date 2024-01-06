#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

// Helper function to search for a manual page in a specific directory
int sectionSearch(char* section, char* command) {
    // Convert section argument to an integer and handle invalid cases like decimals and leading zeros (09)
    int sectionNumber = atoi(section);
    int sectionLength = (int)strlen(section);
    if((sectionNumber < 1 || sectionNumber > 9) || strchr(section, '.') != NULL || sectionLength != 1) {
        printf("invalid section\n");
        return 1;
    }
    // Construct path to manual page file
    char rootDirectory[256] = "./man_pages";
    char directoryToSearch[256];
    char fileToSearch[256];
    char filePath[1024];
    
    sprintf(directoryToSearch, "man%d", sectionNumber);
    sprintf(fileToSearch, "%s.%d", command, sectionNumber);
    sprintf(filePath, "%s/%s/%s", rootDirectory, directoryToSearch, fileToSearch);

    // Try to open file in read mode
    FILE *file = fopen(filePath, "r");

    if (file == NULL) {
        // Error code for file not found. This is handled separately outside the function since the sectionSearch function is also reused in globalSearch
        if (errno == ENOENT) {
            // Different return value to distinguish file not found cases
            return 2;
        }
        // Other file open errors like Permission denied etc
        else {
            printf("cannot open file\n");
            return 1;
        }
    }

    // Parse the file line by line and print it out
    char line[1024];
    while (fgets(line, sizeof(line), file) != NULL) {
        printf("%s", line);
    }

    // Close the file
    fclose(file);

    return 0;
}

// Helper function to search for a manual page across all directories
int globalSearch(char* command) {
    // Iterate over all directories within man_pages
    for(int currentSectionNumber = 1; currentSectionNumber <= 9; currentSectionNumber++) {
        char currentSection[5];
        sprintf(currentSection, "%d", currentSectionNumber);
        // Call sectionSearch helper function for searching within a specific directory
        int res = sectionSearch(currentSection, command);
        // File found and read successfully
        if(res == 0) {
            return 0;
        }
    }
    // File not found in any directory of man_pages
    printf("No manual entry for %s\n", command);
    return 0;
}

int main(int argc, char *argv[]) {
    // Only manual page is provided
    if (argc == 2) {
        int status = globalSearch(argv[1]);
        return status;
    }
    // Subdirectory and manual page is provided, ignore extra args if any
    else if (argc >= 3) {
        int status = sectionSearch(argv[1], argv[2]);
        // Handle file not found scenario
        if (status == 2) {
            printf("No manual entry for %s in section %s\n", argv[2], argv[1]);
            return 0;
        }
        return status;
    }
    // Handle cases with invalid number of arguments provided
    else if (argc == 1) {
        printf("What manual page do you want?\nFor example, try 'wman wman'\n");
    }
    return 0;
}
