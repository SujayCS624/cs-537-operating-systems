#include "wfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>

int main(int argc, char *argv[]) {
    // Check if right number of arguments are provided
    if (argc != 2) {
        printf("Usage: %s <disk_path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *disk_path = argv[1];

    // Open the disk file
    int fd = open(disk_path, O_RDWR);
    if (fd == -1) {
        perror("Error opening disk image");
        exit(EXIT_FAILURE);
    }

    // Find the size of the disk
    int disk_size = lseek(fd, 0, SEEK_END);
    if (disk_size == -1) {
        perror("Error getting disk size");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Map the contents of the disk to memory using mmap
    void *mapped_data = mmap(NULL, disk_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_data == MAP_FAILED) {
        perror("Error mapping disk file into memory");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Initialize the superblock
    struct wfs_sb *sb = (struct wfs_sb *)mapped_data;
    sb->magic = WFS_MAGIC;
    sb->head = 0;

    // Initialize the log entry for the root directory 
    struct wfs_inode root_inode;
    memset(&root_inode, 0, sizeof(struct wfs_inode));
    root_inode.inode_number = 0;
    root_inode.deleted = 0;
    root_inode.mode = __S_IFDIR;
    root_inode.uid = getuid();
    root_inode.gid = getgid();
    root_inode.flags = 0;
    root_inode.size = 0;
    root_inode.atime = time(NULL);
    root_inode.mtime = time(NULL);
    root_inode.ctime = time(NULL);
    root_inode.links = 1;


    struct wfs_log_entry *root_log_entry = (struct wfs_log_entry *)((char*)mapped_data + sizeof(struct wfs_sb));
    memcpy(&root_log_entry->inode, &root_inode, sizeof(struct wfs_inode));

    // Update the head pointer of the superblock
    sb->head += sizeof(struct wfs_log_entry);
    sb->head += sizeof(struct wfs_sb);

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
