#define FUSE_USE_VERSION 30
#include "wfs.h"
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

// Global variables for storing info related to the disk file and its memory mapping
int fd;
void* mapped_data;
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

// Helper function to mark all log entries corresponding to an inode number as deleted
int delete_log_entries(int inode_number) {
    struct wfs_sb *sb = (struct wfs_sb *)mapped_data;
    int flag = 0;

    off_t current_offset = sizeof(struct wfs_sb);
    while (current_offset < sb->head) {
        struct wfs_log_entry *log_entry = (struct wfs_log_entry *)((char *)mapped_data + current_offset);

        if(log_entry->inode.inode_number == inode_number && log_entry->inode.deleted == 0) {
            log_entry->inode.deleted = 1;
            flag = 1;
        }

        current_offset += sizeof(struct wfs_log_entry) + log_entry->inode.size;
    }
    if(flag == 1) {
        return flag;
    }
    else {
        return -1;
    }
}

// Helper function to separate filename and path to the directory that the file is located in
void separate_path_and_file(const char *path, struct wfs_path_info *info) {
    const char *filename = strrchr(path, '/');

    if (filename != NULL) {
        filename++;

        size_t path_len = filename - path - 1;

        strncpy(info->parent_path, path, path_len);
        info->parent_path[path_len] = '\0';

        strncpy(info->filename, filename, MAX_FILE_NAME_LEN - 1);
        info->filename[MAX_FILE_NAME_LEN - 1] = '\0';
    } else {
        strcpy(info->parent_path, "/");
        strncpy(info->filename, path, MAX_FILE_NAME_LEN - 1);
        info->filename[MAX_FILE_NAME_LEN - 1] = '\0';
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


static int wfs_getattr(const char *path, struct stat *stbuf) {
    // Find latest log entry for the given path
    struct wfs_log_entry *log_entry = find_log_entry_by_path(path);
    
    // Check if log entry exists
    if(log_entry == NULL) {
        return -ENOENT;
    }

    // Update the stat structure
    stbuf->st_ino = log_entry->inode.inode_number;
    stbuf->st_mode = log_entry->inode.mode;
    stbuf->st_nlink = log_entry->inode.links;
    stbuf->st_uid = log_entry->inode.uid;
    stbuf->st_gid = log_entry->inode.gid;
    stbuf->st_size = log_entry->inode.size;
    stbuf->st_mtime = log_entry->inode.mtime;
    return 0;
}

static int wfs_mknod(const char *path, mode_t mode, dev_t device) {
    // Find latest log entry for the given path
    struct wfs_log_entry *log_entry = find_log_entry_by_path(path);

    // Check if log entry already exists
    if(log_entry != NULL) {
        return -EEXIST;
    }

    // Separate filename and path to the directory that the file is located in
    char path_copy[MAX_PATH_NAME_LEN];
    strncpy(path_copy, path, MAX_PATH_NAME_LEN);
    struct wfs_path_info path_info;
    separate_path_and_file(path_copy, &path_info);

    // Find latest log entry for the parent directory
    struct wfs_log_entry *parent_log_entry = find_log_entry_by_path(path_info.parent_path);
    
    // Check if parent log entry exists
    if(parent_log_entry == NULL) {
        return -ENOENT;
    }

    // Find the next inode number not in use to assign to the new file
    int new_inode_number = find_max_inode_number() + 1;
    struct wfs_sb *sb = (struct wfs_sb *)mapped_data;
    
    // Check if space exists in the log file system for this operation
    if(sb->head + sizeof(struct wfs_log_entry) + parent_log_entry->inode.size + sizeof(struct wfs_dentry) > disk_size) {
        return -ENOSPC;
    } 
     
    // Construct new parent log entry containing the new file 
    struct wfs_log_entry *new_parent_log_entry = (struct wfs_log_entry *)((char*)mapped_data + sb->head);
    memcpy(new_parent_log_entry, parent_log_entry, sizeof(struct wfs_log_entry) + parent_log_entry->inode.size);
    struct wfs_dentry *entries = (struct wfs_dentry *)new_parent_log_entry->data;
    int num_entries = new_parent_log_entry->inode.size / sizeof(struct wfs_dentry);
    memcpy(entries[num_entries].name, path_info.filename, MAX_FILE_NAME_LEN);
    entries[num_entries].inode_number = new_inode_number;
    new_parent_log_entry->inode.size += sizeof(struct wfs_dentry);
    sb->head += sizeof(struct wfs_log_entry) + new_parent_log_entry->inode.size;

    // Check if space exists in the log file system for this operation
    if(sb->head + sizeof(struct wfs_log_entry) > disk_size) {
        return -ENOSPC;
    }

    // Construct log entry for the new file
    struct wfs_log_entry *new_entry = (struct wfs_log_entry *)((char*)mapped_data + sb->head);
    new_entry->inode.inode_number = new_inode_number;
    new_entry->inode.deleted = 0;
    new_entry->inode.mode = __S_IFREG;
    new_entry->inode.uid = getuid();
    new_entry->inode.gid = getgid();
    new_entry->inode.flags = 0;
    new_entry->inode.size = 0;
    new_entry->inode.atime = time(NULL);
    new_entry->inode.mtime = time(NULL);
    new_entry->inode.ctime = time(NULL);
    new_entry->inode.links = 1;

    sb->head += sizeof(struct wfs_log_entry) + new_entry->inode.size;

    return 0;
}

static int wfs_mkdir(const char *path, mode_t mode) {
    // Find latest log entry for the given path
    struct wfs_log_entry *log_entry = find_log_entry_by_path(path);

    // Check if log entry already exists
    if(log_entry != NULL) {
        return -EEXIST;
    }

    // Separate directory name and path to the directory that the new directory is located in
    char path_copy[MAX_PATH_NAME_LEN];
    strncpy(path_copy, path, MAX_PATH_NAME_LEN);
    struct wfs_path_info path_info;
    separate_path_and_file(path_copy, &path_info);

    // Find latest log entry for the parent directory
    struct wfs_log_entry *parent_log_entry = find_log_entry_by_path(path_info.parent_path);

    // Check if parent log entry exists
    if(parent_log_entry == NULL) {
        return -ENOENT;
    }

    // Find the next inode number not in use to assign to the new directory
    int new_inode_number = find_max_inode_number() + 1;
    struct wfs_sb *sb = (struct wfs_sb *)mapped_data;

    // Check if space exists in the log file system for this operation
    if(sb->head + sizeof(struct wfs_log_entry) + parent_log_entry->inode.size + sizeof(struct wfs_dentry) > disk_size) {
        return -ENOSPC;
    } 

    // Construct new parent log entry containing the new directory
    struct wfs_log_entry *new_parent_log_entry = (struct wfs_log_entry *)((char*)mapped_data + sb->head);
    memcpy(new_parent_log_entry, parent_log_entry, sizeof(struct wfs_log_entry) + parent_log_entry->inode.size);
    struct wfs_dentry *entries = (struct wfs_dentry *)new_parent_log_entry->data;
    int num_entries = new_parent_log_entry->inode.size / sizeof(struct wfs_dentry);
    memcpy(entries[num_entries].name, path_info.filename, MAX_FILE_NAME_LEN);
    entries[num_entries].inode_number = new_inode_number;
    new_parent_log_entry->inode.size += sizeof(struct wfs_dentry);
    sb->head += sizeof(struct wfs_log_entry) + new_parent_log_entry->inode.size;

    // Check if space exists in the log file system for this operation
    if(sb->head + sizeof(struct wfs_log_entry) > disk_size) {
        return -ENOSPC;
    }

    // Construct log entry for the new directory
    struct wfs_log_entry *new_entry = (struct wfs_log_entry *)((char*)mapped_data + sb->head);
    new_entry->inode.inode_number = new_inode_number;
    new_entry->inode.deleted = 0;
    new_entry->inode.mode = __S_IFDIR;
    new_entry->inode.uid = getuid();
    new_entry->inode.gid = getgid();
    new_entry->inode.flags = 0;
    new_entry->inode.size = 0;
    new_entry->inode.atime = time(NULL);
    new_entry->inode.mtime = time(NULL);
    new_entry->inode.ctime = time(NULL);
    new_entry->inode.links = 1;

    sb->head += sizeof(struct wfs_log_entry) + new_entry->inode.size;

    return 0;
}

static int wfs_read(const char *path, char* buffer, size_t size, off_t offset, struct fuse_file_info* info) {
    // Find latest log entry for the given path
    struct wfs_log_entry *log_entry = find_log_entry_by_path(path);

    // Check if log entry exists
    if(log_entry == NULL) {
        return -ENOENT;
    }
    
    // Check if offset is valid and within filesize
    if (offset >= log_entry->inode.size) {
        return 0;
    }

    // Compute the number of bytes that need to be read from the offset
    size_t bytes_to_read;
    if(size > log_entry->inode.size - offset) {
        bytes_to_read = log_entry->inode.size - offset;
    }
    else {
        bytes_to_read = size;
    }

    // Read file contents to the buffer
    memcpy(buffer, log_entry->data + offset, bytes_to_read);
    return bytes_to_read;
}

static int wfs_write(const char *path, const char* buffer, size_t size, off_t offset, struct fuse_file_info* info) {
    // Find latest log entry for the given path
    struct wfs_log_entry *log_entry = find_log_entry_by_path(path);
    
    // Check if log entry exists
    if(log_entry == NULL) {
        return -ENOENT;
    }

    // Compute the new size of the file after the write operation
    int new_size;
    if(offset + size > log_entry->inode.size) {
        new_size = offset + size;
    }
    else {
        new_size = log_entry->inode.size;
    }

    struct wfs_sb *sb = (struct wfs_sb *)mapped_data;

    // Check if space exists in the log file system for this operation
    if(sb->head + sizeof(struct wfs_log_entry) + new_size > disk_size) {
        return -ENOSPC;
    }

    // Construct new entry for the file that is being written to
    struct wfs_log_entry *new_entry = (struct wfs_log_entry *)((char*)mapped_data + sb->head);

    new_entry->inode.inode_number = log_entry->inode.inode_number;
    new_entry->inode.deleted = 0;
    new_entry->inode.mode = __S_IFREG;
    new_entry->inode.uid = getuid();
    new_entry->inode.gid = getgid();
    new_entry->inode.flags = 0;
    new_entry->inode.size = new_size;
    new_entry->inode.atime = time(NULL);
    new_entry->inode.mtime = time(NULL);
    new_entry->inode.ctime = time(NULL);
    new_entry->inode.links = 1;

    memcpy(new_entry->data, log_entry->data, log_entry->inode.size);
    // Write buffer contents to the file
    memcpy(new_entry->data + offset, buffer, size);

    sb->head += sizeof(struct wfs_log_entry) + new_entry->inode.size;

    return size;
}

static int wfs_readdir(const char* path, void* buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* info) {
    // Find latest log entry for the given path
    struct wfs_log_entry *log_entry = find_log_entry_by_path(path);
    
    // Check if log entry exists
    if(log_entry == NULL) {
        return -ENOENT;
    }

    // Check if current path corresponds to a directory
    if(S_ISREG(log_entry->inode.mode)) {
        return -ENOTDIR;
    }

    // Extract the directory entries for the directory
    struct wfs_dentry *entries = (struct wfs_dentry *)log_entry->data;
    int num_entries = log_entry->inode.size / sizeof(struct wfs_dentry);

    // Add entries for . and ..
    filler(buffer, ".", NULL, 0);
    filler(buffer, "..", NULL, 0);

    // Iterate over the directory entries and call the filler function for each entry 
    for (int i = 0; i < num_entries; ++i) {
        filler(buffer, entries[i].name, NULL, 0);
    }
    
    return 0;
}

static int wfs_unlink(const char *path) {
    // Find latest log entry for the given path
    struct wfs_log_entry *log_entry = find_log_entry_by_path(path);
    
    // Check if log entry exists
    if(log_entry == NULL) {
        return -ENOENT;
    }

    // Check if current path corresponds to a directory
    if(S_ISDIR(log_entry->inode.mode)) {
        return -EISDIR;
    }

    // Separate directory name and path to the directory that the new directory is located in
    char path_copy[MAX_PATH_NAME_LEN];
    strncpy(path_copy, path, MAX_PATH_NAME_LEN);
    struct wfs_path_info path_info;
    separate_path_and_file(path_copy, &path_info);

    // Find latest log entry for the parent directory
    struct wfs_log_entry *parent_log_entry = find_log_entry_by_path(path_info.parent_path);
    
    // Check if parent log entry exists
    if(parent_log_entry == NULL) {
        return -ENOENT;
    }

    // Mark all log entries corresponding to an inode number as deleted
    int res = delete_log_entries(log_entry->inode.inode_number);

    struct wfs_sb *sb = (struct wfs_sb *)mapped_data;
    int parent_inode_number = parent_log_entry->inode.inode_number;
    int num_entries = parent_log_entry->inode.size / sizeof(struct wfs_dentry);

    // Check if space exists in the log file system for this operation
    if(sb->head + sizeof(struct wfs_log_entry) + (sizeof(struct wfs_dentry) * (num_entries-1)) > disk_size) {
        return -ENOSPC;
    }

    // Construct new log entry for the parent directory without the deleted file 
    struct wfs_log_entry *new_entry = (struct wfs_log_entry *)((char*)mapped_data + sb->head);
    new_entry->inode.inode_number = parent_inode_number;
    new_entry->inode.deleted = 0;
    new_entry->inode.mode = __S_IFDIR;
    new_entry->inode.uid = getuid();
    new_entry->inode.gid = getgid();
    new_entry->inode.flags = 0;
    new_entry->inode.size = sizeof(struct wfs_dentry) * (num_entries-1);
    new_entry->inode.atime = time(NULL);
    new_entry->inode.mtime = time(NULL);
    new_entry->inode.ctime = time(NULL);
    new_entry->inode.links = 1;

    struct wfs_dentry *old_entries = (struct wfs_dentry *)parent_log_entry->data;
    struct wfs_dentry *new_entries = (struct wfs_dentry *)new_entry->data;

    int curr = 0;

    for (int i = 0; i < num_entries; ++i) {
        if(old_entries[i].inode_number == log_entry->inode.inode_number) {
            continue;
        }
        new_entries[curr].inode_number = old_entries[i].inode_number;
        memcpy(new_entries[curr].name, old_entries[i].name, MAX_FILE_NAME_LEN);
        curr++;
    }


    sb->head += sizeof(struct wfs_log_entry) + new_entry->inode.size;

    if(res == 1) {
        return 0;
    }
    return -ENOENT;
}

static struct fuse_operations ops = {
    .getattr	= wfs_getattr,
    .mknod      = wfs_mknod,
    .mkdir      = wfs_mkdir,
    .read	    = wfs_read,
    .write      = wfs_write,
    .readdir	= wfs_readdir,
    .unlink    	= wfs_unlink,
};


int main(int argc, char *argv[]) {
    
    const char *disk_path = argv[argc-2];

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
    // Modify the arguments before passing them to fuse_main
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    --argc;
    fuse_main(argc, argv, &ops, NULL);
    
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
