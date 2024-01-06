#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "proxyserver.h"
#include "safequeue.h"


/*
 * Constants
 */
#define RESPONSE_BUFSIZE 10000

/*
 * Global configuration variables.
 * Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
int num_listener;
int *listener_ports;
int num_workers;
char *fileserver_ipaddr;
int fileserver_port;
int max_queue_size;
int server_fd;
// Global variable for the priority queue
priority_queue* queue;

void send_error_response(int client_fd, status_code_t err_code, char *err_msg) {
    http_start_response(client_fd, err_code);
    http_send_header(client_fd, "Content-Type", "text/html");
    http_end_headers(client_fd);
    char *buf = malloc(strlen(err_msg) + 2);
    sprintf(buf, "%s\n", err_msg);
    http_send_string(client_fd, buf);
    return;
}

/*
 * forward the client request to the fileserver and
 * forward the fileserver response to the client
 */
void serve_request(int client_fd) {

    // create a fileserver socket
    int fileserver_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fileserver_fd == -1) {
        fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
        exit(errno);
    }

    // create the full fileserver address
    struct sockaddr_in fileserver_address;
    fileserver_address.sin_addr.s_addr = inet_addr(fileserver_ipaddr);
    fileserver_address.sin_family = AF_INET;
    fileserver_address.sin_port = htons(fileserver_port);

    // connect to the fileserver
    int connection_status = connect(fileserver_fd, (struct sockaddr *)&fileserver_address,
                                    sizeof(fileserver_address));
    if (connection_status < 0) {
        // failed to connect to the fileserver
        printf("Failed to connect to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");
        return;
    }

    // successfully connected to the file server
    char *buffer = (char *)malloc(RESPONSE_BUFSIZE * sizeof(char));

    // forward the client request to the fileserver
    int bytes_read = read(client_fd, buffer, RESPONSE_BUFSIZE);
    int ret = http_send_data(fileserver_fd, buffer, bytes_read);
    if (ret < 0) {
        printf("Failed to send request to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");

    } else {
        // forward the fileserver response to the client
        while (1) {
            int bytes_read = recv(fileserver_fd, buffer, RESPONSE_BUFSIZE - 1, 0);
            if (bytes_read <= 0) // fileserver_fd has been closed, break
                break;
            ret = http_send_data(client_fd, buffer, bytes_read);
            if (ret < 0) { // write failed, client_fd has been closed
                break;
            }
        }
    }

    // close the connection to the fileserver
    shutdown(fileserver_fd, SHUT_WR);
    close(fileserver_fd);

    // Free resources and exit
    free(buffer);
}

// Function which gets executed by each worker thread
void *request_work(void* arg) {
    int worker_thread_id = *(int *)arg;
    printf("Worker Thread %d is running\n", worker_thread_id);

    // Loop indefinitely
    while(1) {
        // Retrieve the highest prioirty request from the queue if a request exists
        queue_request request = get_work(queue);

        // printf("Worker %d, Request Client FD: %d, Request Priority: %d Request Delay: %d Request Path: %s\n", worker_thread_id, request.client_fd, request.priority, request.delay, request.path);
        
        // print_queue(queue);

        // Put worker thread to sleep if request has a delay parameter specified
        if(request.delay > 0) {
            // printf("Worker Thread %d is sleeping for %d seconds\n", worker_thread_id, request.delay);
            sleep(request.delay);
        }

        // Serve the request by forwarding it to the file server and returning the response received to the client
        serve_request(request.client_fd);

        shutdown(request.client_fd, SHUT_WR);
        close(request.client_fd);
    }

    pthread_exit(NULL);

}

// Function which gets executed by each listener thread
void *request_listen(void *arg) {
    int listener_thread_id = *(int *)arg;
    printf("Listener Thread %d is running\n", listener_thread_id);

    // create a socket to listen
    int server_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Failed to create a new socket");
        exit(errno);
    }

    // manipulate options for the socket
    int socket_option = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                   sizeof(socket_option)) == -1) {
        perror("Failed to set socket options");
        exit(errno);
    }

    int proxy_port = listener_ports[listener_thread_id];
    // create the full address of this proxyserver
    struct sockaddr_in proxy_address;
    memset(&proxy_address, 0, sizeof(proxy_address));
    proxy_address.sin_family = AF_INET;
    proxy_address.sin_addr.s_addr = INADDR_ANY;
    proxy_address.sin_port = htons(proxy_port); // listening port

    // bind the socket to the address and port number specified in
    if (bind(server_fd, (struct sockaddr *)&proxy_address,
             sizeof(proxy_address)) == -1) {
        perror("Failed to bind on socket");
        exit(errno);
    }

    // starts waiting for the client to request a connection
    if (listen(server_fd, 1024) == -1) {
        perror("Failed to listen on socket");
        exit(errno);
    }

    printf("Listening on port %d...\n", proxy_port);

    struct sockaddr_in client_address;
    size_t client_address_length = sizeof(client_address);
    int client_fd;
    // Loop indefinitely
    while (1) {
        client_fd = accept(server_fd,
                           (struct sockaddr *)&client_address,
                           (socklen_t *)&client_address_length);
        if (client_fd < 0) {
            perror("Error accepting socket");
            continue;
        }

        printf("Listener %d Accepted connection from %s on port %d\n",
                listener_thread_id,
               inet_ntoa(client_address.sin_addr),
               client_address.sin_port);

        // Parse the incoming request
        struct http_request* request = http_request_parse(client_fd);
        // printf("Method: %s\nPath: %s\nDelay: %s\n", request->method, request->path, request->delay);

        int request_priority;
        int isWorkerRequest = -1;
        int delay = atoi(request->delay);
        // Extract the priority of the request
        if (sscanf(request->path, "/%d/", &request_priority) == 1) {
            // printf("Request Priority: %d\n", request_priority);
            isWorkerRequest = 0;
        } else if (strcmp(request->path, "/GetJob") == 0){
            // printf("GetJob request\n");
        }
        else {
            // printf("Unknown request type\n");
            send_error_response(client_fd, BAD_REQUEST, "Unknown request type");
            shutdown(client_fd, SHUT_WR);
            close(client_fd);
            continue;
        }

        // GetJob request is handled by the client itself
        if(isWorkerRequest == -1) {
            // Retrieve the highest prioirty request from the queue if a request exists
            queue_request priority_request = get_work_nonblocking(queue);
            // Queue is empty scenario
            if(priority_request.priority == -1) {
                // printf("Reached Queue is empty scenario\n");
                send_error_response(client_fd, QUEUE_EMPTY, "Priority Queue is empty and GetJob request can't be handled");
                shutdown(client_fd, SHUT_WR);
                close(client_fd);
                continue;
            }
            // Handle GetJob request
            else {
                // printf("Sending response of GetJob request\n");
                send_error_response(client_fd, OK, priority_request.path);
                shutdown(client_fd, SHUT_WR);
                close(client_fd);
                continue;
            }
        }


        // If it isn't a GetJob request, add the request to the priority queue so that it can be picked by a worker thread
        int res = add_work(queue, client_fd, request_priority, delay, request->path);
        if(res == -1) {
            // Queue is full scenario
            // printf("Reached Queue is full scenario\n");
            send_error_response(client_fd, QUEUE_FULL, "Priority Queue is full and request can't be handled");
            shutdown(client_fd, SHUT_WR);
            close(client_fd);
            continue;
        }
        printf("Highest Priority Request: %d\n", peek(queue));
        print_queue(queue);
    }

    shutdown(server_fd, SHUT_RDWR);
    close(server_fd);

    pthread_exit(NULL);
}

/*
 * opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *server_fd) {

    // create a socket to listen
    *server_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (*server_fd == -1) {
        perror("Failed to create a new socket");
        exit(errno);
    }

    // manipulate options for the socket
    int socket_option = 1;
    if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                   sizeof(socket_option)) == -1) {
        perror("Failed to set socket options");
        exit(errno);
    }


    int proxy_port = listener_ports[0];
    // create the full address of this proxyserver
    struct sockaddr_in proxy_address;
    memset(&proxy_address, 0, sizeof(proxy_address));
    proxy_address.sin_family = AF_INET;
    proxy_address.sin_addr.s_addr = INADDR_ANY;
    proxy_address.sin_port = htons(proxy_port); // listening port

    // bind the socket to the address and port number specified in
    if (bind(*server_fd, (struct sockaddr *)&proxy_address,
             sizeof(proxy_address)) == -1) {
        perror("Failed to bind on socket");
        exit(errno);
    }

    // starts waiting for the client to request a connection
    if (listen(*server_fd, 1024) == -1) {
        perror("Failed to listen on socket");
        exit(errno);
    }

    printf("Listening on port %d...\n", proxy_port);

    struct sockaddr_in client_address;
    size_t client_address_length = sizeof(client_address);
    int client_fd;
    while (1) {
        client_fd = accept(*server_fd,
                           (struct sockaddr *)&client_address,
                           (socklen_t *)&client_address_length);
        if (client_fd < 0) {
            perror("Error accepting socket");
            continue;
        }

        serve_request(client_fd);
        // close the connection to the client
        shutdown(client_fd, SHUT_WR);
        close(client_fd);
    }

    shutdown(*server_fd, SHUT_RDWR);
    close(*server_fd);
}

/*
 * Default settings for in the global configuration variables
 */
void default_settings() {
    num_listener = 1;
    listener_ports = (int *)malloc(num_listener * sizeof(int));
    listener_ports[0] = 8000;

    num_workers = 1;

    fileserver_ipaddr = "127.0.0.1";
    fileserver_port = 3333;

    max_queue_size = 100;
}

void print_settings() {
    printf("\t---- Setting ----\n");
    printf("\t%d listeners [", num_listener);
    for (int i = 0; i < num_listener; i++)
        printf(" %d", listener_ports[i]);
    printf(" ]\n");
    printf("\t%d workers\n", num_workers);
    printf("\tfileserver ipaddr %s port %d\n", fileserver_ipaddr, fileserver_port);
    printf("\tmax queue size  %d\n", max_queue_size);
    printf("\t  ----\t----\t\n");
}

void signal_callback_handler(int signum) {
    printf("Caught signal %d: %s\n", signum, strsignal(signum));
    for (int i = 0; i < num_listener; i++) {
        if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
    }
    free(listener_ports);
    exit(0);
}

char *USAGE =
    "Usage: ./proxyserver [-l 1 8000] [-n 1] [-i 127.0.0.1 -p 3333] [-q 100]\n";

void exit_with_usage() {
    fprintf(stderr, "%s", USAGE);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    signal(SIGINT, signal_callback_handler);

    /* Default settings */
    default_settings();

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp("-l", argv[i]) == 0) {
            num_listener = atoi(argv[++i]);
            free(listener_ports);
            listener_ports = (int *)malloc(num_listener * sizeof(int));
            for (int j = 0; j < num_listener; j++) {
                listener_ports[j] = atoi(argv[++i]);
            }
        } else if (strcmp("-w", argv[i]) == 0) {
            num_workers = atoi(argv[++i]);
        } else if (strcmp("-q", argv[i]) == 0) {
            max_queue_size = atoi(argv[++i]);
        } else if (strcmp("-i", argv[i]) == 0) {
            fileserver_ipaddr = argv[++i];
        } else if (strcmp("-p", argv[i]) == 0) {
            fileserver_port = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
            exit_with_usage();
        }
    }
    print_settings();


    // Create a priority queue of max queue size
    queue =  create_queue(max_queue_size);

    pthread_t listener_threads[num_listener];
    pthread_t worker_threads[num_workers];

    // Create listener threads
    for(int i = 0; i < num_listener; i++) {
        int* arg = malloc(sizeof(int));
        *arg = i;
        if(pthread_create(&listener_threads[i], NULL, request_listen, arg) != 0) {
            perror("Unable to create listener threads");
            exit(1);
        };
    }

    // Create worker threads
    for(int i = 0; i < num_workers; i++) {
        int* arg = malloc(sizeof(int));
        *arg = i;
        if(pthread_create(&worker_threads[i], NULL, request_work, arg) != 0) {
            perror("Unable to create worker threads");
            exit(1);
        };
    }

    // Wait for listener threads
    for(int i = 0; i < num_listener; i++) {
        if(pthread_join(listener_threads[i], NULL) != 0) {
            perror("Unable to wait for listener threads");
            exit(1);
        }
    }

    // Wait for worker threads
    for(int i = 0; i < num_listener; i++) {
        if(pthread_join(worker_threads[i], NULL) != 0) {
            perror("Unable to wait for worker threads");
            exit(1);
        }
    }

    printf("All threads have finished.\n");
    
    return EXIT_SUCCESS;
}
