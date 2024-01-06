#include <pthread.h>
#ifndef SAFEQUEUE_H
#define SAFEQUEUE_H

#define MAX_QUEUE_SIZE 1000

typedef struct {
    int client_fd; // Store the client FD
    int priority; // Store the priority of the request
    int delay; // Store the value of delay header sent with the request
    char *path; // Store the path of the request
} queue_request;

typedef struct {
    queue_request requests[MAX_QUEUE_SIZE];
    int max_size;
    int curr_size;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} priority_queue;

priority_queue* create_queue(int queue_size);
int add_work(priority_queue* queue, int client_fd, int priority, int delay, char* path);
int peek(priority_queue* queue);
queue_request get_work(priority_queue* queue);
queue_request get_work_nonblocking(priority_queue* queue);
void delete_queue(priority_queue* queue);
void print_queue(priority_queue* queue);

#endif