#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>

#define MAX_CLIENTS 8
#define PORT 8005
#define BUFFER_SIZE 1024

typedef struct {
    char name[256];
    pid_t pid;
    unsigned long long utime;  // User time in milliseconds
    unsigned long long stime;  // System time in milliseconds
    unsigned long long total_time;  // Total CPU time in milliseconds
} ProcessInfo;

// Function to read /proc/[pid]/stat and extract process information
int read_process_stat(pid_t pid, ProcessInfo *proc_info) {
    char path[256];
    sprintf(path, "/proc/%d/stat", pid);

    FILE *file = fopen(path, "r");
    if (!file) {
        return -1; // Could not open the file
    }

    // Read the contents of the stat file
    char comm[256];
    unsigned long long utime, stime;
    if (fscanf(file, "%d (%[^)]) %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu",
               &proc_info->pid, comm, &utime, &stime) != 4) {
        fclose(file);
        return -1;
    }

    strncpy(proc_info->name, comm, sizeof(proc_info->name) - 1);
    proc_info->name[sizeof(proc_info->name) - 1] = '\0';

    // Convert clock ticks to milliseconds
    long clock_ticks = sysconf(_SC_CLK_TCK);
    proc_info->utime = utime * 1000 / clock_ticks;
    proc_info->stime = stime * 1000 / clock_ticks;
    proc_info->total_time = proc_info->utime + proc_info->stime;

    fclose(file);
    return 0;
}

// Function to compare two ProcessInfo structures for sorting
int compare_processes(const void *a, const void *b) {
    return ((ProcessInfo *)b)->total_time - ((ProcessInfo *)a)->total_time;
}

// Function to get top 2 CPU-consuming processes
void get_top_processes(char *buffer, size_t buffer_size) {
    struct dirent *entry;
    DIR *dp = opendir("/proc");
    ProcessInfo processes[1024];
    int count = 0;

    if (!dp) {
        snprintf(buffer, buffer_size, "Failed to open /proc directory");
        return;
    }

    // Read the /proc directory
    while ((entry = readdir(dp)) != NULL) {
        // Check if the entry is a PID (directory name is all digits)
        if (entry->d_type == DT_DIR && atoi(entry->d_name) > 0) {
            pid_t pid = atoi(entry->d_name);
            if (read_process_stat(pid, &processes[count]) == 0) {
                count++;
            }
        }
    }

    closedir(dp);

    // Sort processes by CPU time
    qsort(processes, count, sizeof(ProcessInfo), compare_processes);

    // Prepare output for top 2 CPU-consuming processes
    snprintf(buffer, buffer_size, "Top 2 CPU-consuming processes:\n");
    for (int i = 0; i < 2 && i < count; i++) {
        snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer), 
                "PID: %d, Name: %s, User Time: %.2fs, System Time: %.2fs, Total Time: %.2fs\n",
                processes[i].pid, processes[i].name, 
                processes[i].utime / 1000.0, processes[i].stime / 1000.0, processes[i].total_time / 1000.0);
    }
}

void *handle_client(void *socket_ptr) {
    int client_socket = *(int *)socket_ptr;
    free(socket_ptr);
    
    char buffer[BUFFER_SIZE] = {0};
    char response[2048] = {0};

    // Read message from the client
    ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("Message received: %s\n", buffer);

        // Get top CPU-consuming processes
        get_top_processes(response, sizeof(response));

        // Send the response back to the client
        send(client_socket, response, strlen(response), 0);
        printf("Top CPU-consuming processes sent\n");
    }
    close(client_socket);
    return NULL;
}

int main() {
    int server_fd, new_socket, max_fd, activity;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    fd_set read_fds;
    int client_sockets[MAX_CLIENTS] = {0};
    
    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    // Bind server socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen on server socket
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Server is listening on port %d\n", PORT);

    while (1) {
        // Clear the socket set
        FD_ZERO(&read_fds);
        
        // Add the server socket to the set
        FD_SET(server_fd, &read_fds);
        max_fd = server_fd;
        
        // Add client sockets to the set
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            if (sd > 0) {
                FD_SET(sd, &read_fds);
            }
            if (sd > max_fd) {
                max_fd = sd;
            }
        }
        
        // Wait for activity on any of the sockets
        activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        
        if ((activity < 0) && (errno != EINTR)) {
            perror("Select error");
            continue;
        }
        
        // Check if there's activity on the server socket (new connection)
        if (FD_ISSET(server_fd, &read_fds)) {
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
                perror("Accept failed");
                continue;
            }
            
            // Add new socket to client_sockets array
            int i;
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = new_socket;
                    printf("New connection, socket fd is %d\n", new_socket);
                    break;
                }
            }
            if (i == MAX_CLIENTS) {
                printf("Max clients reached. Connection rejected.\n");
                close(new_socket);
            }
        }
        
        // Check for I/O operations on client sockets
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            
            if (FD_ISSET(sd, &read_fds)) {
                pthread_t thread_id;
                int *pclient = malloc(sizeof(int));
                *pclient = sd;
                
                // Create a new thread to handle the client
                if (pthread_create(&thread_id, NULL, handle_client, pclient) != 0) {
                    perror("Thread creation failed");
                    free(pclient);
                    close(sd);
                }
                client_sockets[i] = 0; // Remove the client socket from the array
            }
        }
    }

    close(server_fd);
    return 0;
}