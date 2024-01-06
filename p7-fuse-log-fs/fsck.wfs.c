#include "wfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>

// Global variables for storing info related to the disk file and its memory mapping
int fd;
void* mapped_data;
void* new_mapped_data;
int disk_size;

// Helper function to print all entries of the log structured filesystem
void print_log_entries() {
    struct wfs_sb *sb = (struct wfs_sb *)mapped_data;

    off_t current_offset = sizeof(struct wfs_sb);
    while (current_offset < sb->head) {
        struct wfs_log_entry *log_entry = (struct wfs_log_entry *)((char *)mapped_data + current_offset);

        if(log_entry->inode.deleted == 1) {
            current_offset += sizeof(struct wfs_log_entry) + log_entry->inode.size;
            continue;
        }

        printf("Inode Number: %u, Mode: %u, Size: %u\n", log_entry->inode.inode_number, log_entry->inode.mode, log_entry->inode.size);

        if (S_ISDIR(log_entry->inode.mode)) {
            struct wfs_dentry *entries = (struct wfs_dentry *)log_entry->data;
            int num_entries = log_entry->inode.size / sizeof(struct wfs_dentry);

            for (int i = 0; i < num_entries; ++i) {
                printf("  Entry: %s (Inode: %lu)\n", entries[i].name, entries[i].inode_number);
            }
        } else if (S_ISREG(log_entry->inode.mode)) {
            printf("This is a file\n");
        }

        current_offset += sizeof(struct wfs_log_entry) + log_entry->inode.size;
    }
}

// Helper function to find the most recent log entry for an inode number
struct wfs_log_entry* find_latest_log_entry(unsigned int inode_number) {
    struct wfs_sb *sb = (struct wfs_sb *)mapped_data;
    struct wfs_log_entry* latest_entry = NULL;

    off_t current_offset = sizeof(struct wfs_sb);
    while (current_offset < sb->head) {
        struct wfs_log_entry *log_entry = (struct wfs_log_entry *)((char *)mapped_data + current_offset);

        if(log_entry->inode.inode_number == inode_number && log_entry->inode.deleted == 0) {
            latest_entry = log_entry;
        }

        current_offset += sizeof(struct wfs_log_entry) + log_entry->inode.size;
    }
    return latest_entry;
}

// Helper function to find the most recent log entry for a given path
struct wfs_log_entry* find_log_entry_by_path(const char *path) {
    struct wfs_sb *sb = (struct wfs_sb *)mapped_data;
    struct wfs_log_entry *found_entry = NULL;
    off_t current_offset = sizeof(struct wfs_sb);
    struct wfs_log_entry *log_entry = (struct wfs_log_entry *)((char *)mapped_data + sizeof(struct wfs_sb));

    char *token, *saveptr;
    char path_copy[MAX_PATH_NAME_LEN];
    strncpy(path_copy, path, MAX_PATH_NAME_LEN);

    token = strtok_r(path_copy, "/", &saveptr);

    if(token == NULL) {
        int inode_number = log_entry->inode.inode_number;
        return find_latest_log_entry(inode_number);
    }

    while (token != NULL && current_offset < sb->head) {
        log_entry = (struct wfs_log_entry *)((char *)mapped_data + current_offset);

        if (S_ISDIR(log_entry->inode.mode)) {
            struct wfs_dentry *entries = (struct wfs_dentry *)log_entry->data;
            int num_entries = log_entry->inode.size / sizeof(struct wfs_dentry);

            int i;
            int flag = 0;
            for (i = 0; i < num_entries; ++i) {
                if (strcmp(entries[i].name, token) == 0) {
                    struct wfs_log_entry *temp = find_latest_log_entry(entries[i].inode_number);
                    if(temp == NULL) {
                        continue;
                    }
                    log_entry = temp;
                    current_offset = (char*)log_entry - (char*)mapped_data;
                    token = strtok_r(NULL, "/", &saveptr);
                    flag = 1;
                    break;
                }
            }
            if(flag == 1) {
                continue;
            }
        }
        current_offset += sizeof(struct wfs_log_entry) + log_entry->inode.size;
    }
    if(token == NULL) {
        found_entry = log_entry; 
    }
    return found_entry;
}

// Helper function to find the highest inode number used by the log file system so far
int find_max_inode_number() {
    int max_inode_number = 0;
    struct wfs_sb *sb = (struct wfs_sb *)mapped_data;

    off_t current_offset = sizeof(struct wfs_sb);
    while (current_offset < sb->head) {
        struct wfs_log_entry *log_entry = (struct wfs_log_entry *)((char *)mapped_data + current_offset);

        if(log_entry->inode.inode_number > max_inode_number) {
            max_inode_number = log_entry->inode.inode_number;
        }

        current_offset += sizeof(struct wfs_log_entry) + log_entry->inode.size;
    }
    return max_inode_number;
}

int main(int argc, char *argv[]) {
    // Check if right number of arguments are provided
    if (argc != 2) {
        printf("Usage: %s <disk_path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *disk_path = argv[1];

    // Open the disk file
    fd = open(disk_path, O_RDWR);
    if (fd == -1) {
        perror("Error opening disk image");
        exit(EXIT_FAILURE);
    }

    // Find the size of the disk
    disk_size = lseek(fd, 0, SEEK_END);
    if (disk_size == -1) {
        perror("Error getting disk size");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Map the contents of the disk to memory using mmap
    mapped_data = mmap(NULL, disk_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_data == MAP_FAILED) {
        perror("Error mapping disk file into memory");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Allocate and initialize memory for the new compacted version of the log file system
    new_mapped_data = malloc(disk_size);
    memset(new_mapped_data, 0, disk_size);

    // Initialize the superblock
    struct wfs_sb *new_sb = (struct wfs_sb *)new_mapped_data;
    new_sb->magic = WFS_MAGIC;
    new_sb->head = 0;
    new_sb->head += sizeof(struct wfs_sb);

    // Find the highest inode number used by the log file system so far
    int max_inode_number = find_max_inode_number();

    // Iterate over all inode numbers assigned
    for(int i = 0; i <= max_inode_number; i++) {
        // Find the most recent log entry for an inode number
        struct wfs_log_entry* log_entry = find_latest_log_entry(i);
        
        // If log entry doesn't exist or is deleted, continue
        if(log_entry == NULL || log_entry->inode.deleted == 1) {
            continue;
        }

        // Copy only the most recent log entry for the inode number to the new compacted disk
        struct wfs_log_entry *new_log_entry = (struct wfs_log_entry *)((char*)new_mapped_data + new_sb->head);
        memcpy(new_log_entry, log_entry, sizeof(struct wfs_log_entry) + log_entry->inode.size);
        new_sb->head += sizeof(struct wfs_log_entry) + log_entry->inode.size;
    }

    // Update the memory mapping of the disk to equal the compacted mapping
    memset(mapped_data, 0, disk_size);
    memcpy(mapped_data, new_mapped_data, disk_size);

    // Unmap the memory mapping
    if (munmap(mapped_data, disk_size) == -1) {
        perror("Error unmapping memory");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Close the disk file
    close(fd);

    return 0;
}