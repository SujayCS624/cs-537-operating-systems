#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include "safequeue.h"

// Function to create an empty priority queue
priority_queue* create_queue(int queue_size) {
    priority_queue* queue = (priority_queue*)malloc(sizeof(priority_queue));
    if (queue == NULL) {
        perror("Failed to allocate memory for the queue");
        exit(1);
    }

    queue->curr_size = 0;
    queue->max_size = queue_size;
    for(int i = 0; i < MAX_QUEUE_SIZE; i++) {
        queue->requests[i].client_fd = -1;
        queue->requests[i].priority = -1;
        queue->requests[i].delay = 0;
        queue->requests[i].path = malloc(1000);
        queue->requests[i].path = "";
    }
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);

    return queue;
}

// Function to add a new request into the priority queue
int add_work(priority_queue* queue, int client_fd, int priority, int delay, char* path) {
    pthread_mutex_lock(&queue->mutex);

    if (queue->curr_size == queue->max_size) {
        // printf("Queue is full\n");
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    queue->requests[queue->curr_size].client_fd = client_fd;
    queue->requests[queue->curr_size].priority = priority;
    queue->requests[queue->curr_size].delay = delay;
    queue->requests[queue->curr_size].path = path;
    queue->curr_size++;

    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    return 0; 
}

// Function to take a look at the highest priority request in the priority queue
int peek(priority_queue* queue) {
    pthread_mutex_lock(&queue->mutex);

    int highestPriority = -1;
    int index = -1;

    if(queue->curr_size == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return index;
    }

    for (int i = 0; i < queue->curr_size; i++) { 
        if (highestPriority < queue->requests[i].priority) {
            highestPriority = queue->requests[i].priority;
            index = i;
        }
    }

    pthread_mutex_unlock(&queue->mutex);
    return index;
}

// Function to print the priority queue for debugging purposes
void print_queue(priority_queue* queue) {
    pthread_mutex_lock(&queue->mutex);
    for(int i = 0; i < queue->curr_size; i++) {
        printf("Request Number: %d, Client FD: %d, Priority: %d Delay: %d Path: %s\n", i+1, queue->requests[i].client_fd, queue->requests[i].priority, queue->requests[i].delay, queue->requests[i].path);
    }
    pthread_mutex_unlock(&queue->mutex);
}

// Function to remove the highest priority request from the priority queue and return it to the calling function
queue_request get_work(priority_queue* queue) {
    pthread_mutex_lock(&queue->mutex);

    while(queue->curr_size == 0) {
        pthread_cond_wait(&queue->cond, &queue->mutex);    
    }

    int highestPriority = -1;
    int index = -1;

    for (int i = 0; i < queue->curr_size; i++) { 
        if (highestPriority < queue->requests[i].priority) {
            highestPriority = queue->requests[i].priority;
            index = i;
        }
    }
    
    queue_request next_request;
    next_request.client_fd = queue->requests[index].client_fd;
    next_request.priority = queue->requests[index].priority;
    next_request.delay = queue->requests[index].delay;
    next_request.path = malloc(1000);
    next_request.path = queue->requests[index].path;

    for (int i = index; i < queue->curr_size; i++) {
        queue->requests[i] = queue->requests[i+1];
    }

    queue->curr_size--;

    pthread_mutex_unlock(&queue->mutex);

    return next_request;
}

// Function to remove the highest priority request from the priority queue and return it to the calling function (non-blocking version)
queue_request get_work_nonblocking(priority_queue* queue) {
    pthread_mutex_lock(&queue->mutex);

    queue_request next_request;

    if(queue->curr_size == 0) {
        next_request.client_fd = -1;
        next_request.priority = -1;
        next_request.delay = -1;
        next_request.path = malloc(1000);
        next_request.path = "";
        // printf("Queue is empty\n");
        pthread_mutex_unlock(&queue->mutex);
        return next_request;   
    }

    int highestPriority = -1;
    int index = -1;

    for (int i = 0; i < queue->curr_size; i++) { 
        if (highestPriority < queue->requests[i].priority) {
            highestPriority = queue->requests[i].priority;
            index = i;
        }
    }
    
    next_request.client_fd = queue->requests[index].client_fd;
    next_request.priority = queue->requests[index].priority;
    next_request.delay = queue->requests[index].delay;
    next_request.path = malloc(1000);
    next_request.path = queue->requests[index].path;

    for (int i = index; i < queue->curr_size; i++) {
        queue->requests[i] = queue->requests[i+1];
    }

    queue->curr_size--;

    pthread_mutex_unlock(&queue->mutex);

    return next_request;

}

// Function to delete the priority queue
void delete_queue(priority_queue* queue) {
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
    free(queue);
}