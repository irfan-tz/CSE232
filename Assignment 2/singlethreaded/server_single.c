#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

// Structure to store process information
typedef struct {
    char name[256];
    pid_t pid;
    unsigned long user_time;  // User time
    unsigned long kernel_time;  // System time
    unsigned long total_time;  // Total CPU time
} Process;

// Function to read /proc/[pid]/stat and extract process information
int read_proc_file(pid_t pid, Process *proc_info) {
    char path[256];
    sprintf(path, "/proc/%d/stat", pid);

    // Open the /proc/[pid]/stat file for reading
    FILE *file = fopen(path, "r");
    if (!file) {
        return -1; // Could not open the file
    }

    // Read the contents of the stat file
    fscanf(file, "%d %s %*c %*d %*d %*d %*d %*d %lu %lu",
           &proc_info->pid, proc_info->name, &proc_info->user_time, &proc_info->kernel_time);

    // Remove parentheses from process name
    char *name_end = strchr(proc_info->name, ')');
    if (name_end) {
        *name_end = '\0';
    }

    // Calculate total CPU time
    proc_info->total_time = proc_info->user_time + proc_info->kernel_time;

    fclose(file);
    return 0;
}

// Function to get top 2 CPU-consuming processes
void get_top_processes(char *buffer, size_t buffer_size) {
    struct dirent *entry;
    DIR *dp = opendir("/proc");
    Process processes[1024];
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
            if (read_proc_file(pid, &processes[count]) == 0) {
                count++;
            }
        }
    }

    closedir(dp);

    // Manual Bubble Sort to sort processes by total_time
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (processes[j].total_time < processes[j + 1].total_time) {
                // Swap the two processes
                Process temp = processes[j];
                processes[j] = processes[j + 1];
                processes[j + 1] = temp;
            }
        }
    }

    // Prepare output for top 2 CPU-consuming processes
    snprintf(buffer, buffer_size, "Top 2 CPU-consuming processes:\n");
    for (int i = 0; i < 2 && i < count; i++) {
        snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
                 "PID: %d, Name: %s, User Time: %lu, System Time: %lu, Total Time: %lu\n",
                 processes[i].pid, processes[i].name, processes[i].user_time, processes[i].kernel_time, processes[i].total_time);
    }
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
    char response[2048] = {0}; // Buffer for the response

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8002);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port 8002...\n");

    // Accept only one connection
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }

    // Read message from the client
    read(new_socket, buffer, 1024);
    printf("Message received: %s\n", buffer);

    // Get top CPU-consuming processes
    get_top_processes(response, sizeof(response));

    // Send the response back to the client
    send(new_socket, response, strlen(response), 0);
    printf("Top CPU-consuming processes sent\n");

    close(new_socket);  // Close the connection with the client
    close(server_fd);   // Close the server socket

    printf("Server has shut down after handling one request.\n");

    return 0;
}

