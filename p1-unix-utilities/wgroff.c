#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <regex.h>
#include <ctype.h>

// Helper function to handle formatting for all the normal lines of the input file
void convertFormattingMarks(char *line) {
    // Array of characters to be converted
    char *find[] = {"/fB", "/fI", "/fU", "/fP", "//"};
    // Array of characters to convert to
    char *replace[] = {"\033[1m", "\033[3m", "\033[4m", "\033[0m", "/"};

    // Repeat process for each index in the array above
    for (int i = 0; i < 5; i++) {
        // Character to find
        char *findCharacter = find[i];
        // Character to replace
        char *replaceCharacter = replace[i];
        // Length of character to find
        int findLength = (int)strlen(findCharacter);
        // Length of character to replace
        int replaceLength = (int)strlen(replaceCharacter);
        char result[1024];
        int resultIndex = 0;
        // Loop through the line of input
        for (int j = 0; line[j] != '\0'; j++) {
        // If character is found, replace it with new character in result line
        if (strncmp(&line[j], findCharacter, findLength) == 0) {
            strcpy(&result[resultIndex], replaceCharacter);
            resultIndex += replaceLength;
            j += findLength - 1;
        } 
        // Copy character as it is to the result line
        else {
            result[resultIndex] = line[j];
            resultIndex++;
        }
    }
    // Null terminate the result line
    result[resultIndex] = '\0';
    // Overwrite input line with result line
    strcpy(line, result);
    }

}

int convertFile(char* readFileName) {
    // Construct path to file to be read
    char readFilePath[512];
    sprintf(readFilePath, "./%s", readFileName);
    // Open file to be read and handle error case
    FILE *readFile = fopen(readFilePath, "r");
    if (readFile == NULL) {
        // File not found case
        if (errno == ENOENT) {
            printf("File doesn't exist\n");
            return 0;
        }
        // Other file opening errors
        printf("Cannot open file\n");
        return 1;
    }

    char line[1024];
    int formatErrorFlag = 0;
    int lineNumber = 1;
    // Read first line of the file
    fgets(line, sizeof(line), readFile);
    char command[256];
    char section[256];
    char date[256];
    // Regular expression pattern for the first line of the file
    const char *headerPattern = "^\\.TH\\s(\\S+)\\s([1-9])\\s([0-9]{4}-[0-9]{2}-[0-9]{2})\n$";

    // Structure for regex handling
    regex_t regex;
    // Store return code
    int reti;
    // Store matched substrings
    regmatch_t pmatch[4];

    // Compile the regex pattern
    reti = regcomp(&regex, headerPattern, REG_EXTENDED);
    if (reti) {
        fprintf(stderr, "Could not compile regex\n");
        exit(1);
    }

    // Execute the regex match against line
    reti = regexec(&regex, line, 4, pmatch, 0);
    // reti is 0 if regex match is successful
    if (!reti) {
        // Extract the matched parts
        strncpy(command, line + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so);
        command[pmatch[1].rm_eo - pmatch[1].rm_so] = '\0';
        strncpy(section, line + pmatch[2].rm_so, pmatch[2].rm_eo - pmatch[2].rm_so);
        section[pmatch[2].rm_eo - pmatch[2].rm_so] = '\0';
        strncpy(date, line + pmatch[3].rm_so, pmatch[3].rm_eo - pmatch[3].rm_so);
        date[pmatch[3].rm_eo - pmatch[3].rm_so] = '\0';
    } 
    // Line didn't match the regex pattern
    else if (reti == REG_NOMATCH) {
        formatErrorFlag = 1;
    } 
    // Handle unexpected execution errors
    else {
        char error_buffer[100];
        regerror(reti, &regex, error_buffer, sizeof(error_buffer));
        fprintf(stderr, "Regex match failed: %s\n", error_buffer);
        exit(1);
    }

    // Free the compiled regex
    regfree(&regex);

    // Print message and exit if the first line of the input is not in the appropriate format
    if(formatErrorFlag == 1) {
        printf("Improper formatting on line %d\n", lineNumber);
        return 0;
    }

    // Construct path to file to be written
    char writeFilePath[1024];
    sprintf(writeFilePath, "./%s.%s", command, section);
    // Open file to be written and handle error case
    FILE *writeFile = fopen(writeFilePath, "w");
    if (writeFile == NULL) {
        printf("Cannot open file\n");
        return 1;
    }
    // Construct content to be written to first and last line of output file
    char firstLineContent[1024];
    char lastLineContent[1024];
    sprintf(firstLineContent, "%s(%s)", command, section);
    sprintf(lastLineContent, "%*s%s%*s", 35, "", date, 35, "");
    int firstLineContentLength = (int)strlen(firstLineContent);
    int firstLineSpacesCount = 80 - 2*firstLineContentLength;
    // Write first line content to the output file
    fprintf(writeFile, "%s%*s%s\n", firstLineContent, firstLineSpacesCount, "", firstLineContent);
    
    // Iterate over each line in the file 
    while (fgets(line, sizeof(line), readFile) != NULL) {
        lineNumber = lineNumber + 1;
        char sectionName[256];
        char hashTagContent[256];
        // Ignore if line starts with a hashtag
        if (sscanf(line, "#%s", hashTagContent) == 1) {
            continue;
        }
        // Handle case when line is a section header
        else if (sscanf(line, ".SH %[^\n]", sectionName) == 1) {
            fprintf(writeFile, "\n");
            int len = (int)strlen(sectionName);
            // Convert to upper case
            for (int i = 0; i < len; i++) {
                sectionName[i] = toupper((unsigned char)sectionName[i]);
            }
            // Convert to bold and write to output file
            fprintf(writeFile, "\033[1m%s\033[0m\n", sectionName);
        }
        // Handle invalid case when section header is a newline character
        else if (strncmp(line, ".SH\n", 4) == 0) {
            printf("Improper formatting on line %d\n", lineNumber);
            return 0;
        }
        else {
            // Append 7 whitespaces at the start of the line
            fprintf(writeFile, "%7s", "");
            // Call helper function to convert formatting for other lines
            convertFormattingMarks(line);
            // Write converted line to output file
            fprintf(writeFile, "%s", line);
        }
     }
    
    // Write last line content to the output file
    fprintf(writeFile, "%s\n", lastLineContent);

    fclose(readFile);
    fclose(writeFile);
    return 0;
}

int main(int argc, char *argv[]) {
    // Argument provided, ignore extra args if any
    if (argc >= 2) {
        int status = convertFile(argv[1]);
        return status;
    }
    // Handle cases with invalid number of arguments provided
    else if (argc == 1) {
        printf("Improper number of arguments\nUsage: ./wgroff <file>\n");
    }
    return 0;
}