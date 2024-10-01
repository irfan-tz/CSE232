#include <stdio.h>          // Include for standard input and output functions
#include <stdlib.h>        // Include for general purpose functions like atoi
#include <string.h>        // Include for string manipulation functions
#include <unistd.h>        // Include for POSIX operating system API (like close)
#include <arpa/inet.h>     // Include for network-related functions (like inet_pton)
#include <pthread.h>       // Include for using pthreads (POSIX threads)

#define BUFFER_SIZE 1024   // Define buffer size for communication

// Structure to hold thread arguments
typedef struct {
    int client_id;         // Client identifier
    const char *message;   // Message to send to the server
} ClientArgs;

// Function to run as a thread for each client
void *client_task(void *args) {
    ClientArgs *client_args = (ClientArgs *)args; // Cast the argument to ClientArgs
    int sock = 0;                                 // Socket descriptor for the client
    struct sockaddr_in serv_addr;                 // Structure to hold server address information
    char buffer[BUFFER_SIZE] = {0};                // Buffer to store the server's response

    // Create a TCP socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Thread %d: Socket creation error\n", client_args->client_id); // Print error if socket creation fails
        return NULL;                              // Exit the thread if socket creation fails
    }

    serv_addr.sin_family = AF_INET;               // Set the address family to IPv4
    serv_addr.sin_port = htons(8005);             // Set the server port to 8005

    // Convert the IP address from text to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("Thread %d: Invalid address/Address not supported\n", client_args->client_id); // Print error if address conversion fails
        return NULL;                              // Exit the thread if address conversion fails
    }

    // Establish a connection to the server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Thread %d: Connection Failed\n", client_args->client_id); // Print error if connection fails
        return NULL;                              // Exit the thread if connection fails
    }

    // Send the request message to the server
    send(sock, client_args->message, strlen(client_args->message), 0);
    printf("Thread %d: Hello message sent\n", client_args->client_id); // Indicate the message was sent

    // Read the server's response into the buffer
    read(sock, buffer, BUFFER_SIZE);
    printf("Thread %d: Message received:\n%s\n", client_args->client_id, buffer); // Print the received message

    close(sock);                                  // Close the socket after communication
    free(client_args);                            // Free the allocated memory for client_args
    return NULL;                                  // Exit the thread
}

// Main function
int main(int argc, char *argv[]) {
    // Check if the correct number of command-line arguments is provided
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <number_of_clients>\n", argv[0]); // Print usage message
        return 1;                             // Exit the program with an error code
    }

    int num_clients = atoi(argv[1]);              // Convert the command-line argument to an integer
    pthread_t *threads = malloc(num_clients * sizeof(pthread_t)); // Allocate memory for thread IDs

    // Create threads for the number of clients specified
    for (int i = 0; i < num_clients; i++) {
        ClientArgs *args = malloc(sizeof(ClientArgs)); // Allocate memory for thread arguments
        args->client_id = i;                           // Set the client identifier
        args->message = "Requesting CPU info from server"; // Set the message to send

        // Start a new thread for each client task
        if (pthread_create(&threads[i], NULL, client_task, args) != 0) {
            fprintf(stderr, "Error creating thread %d\n", i); // Print error if thread creation fails
            free(args); // Free the allocated memory on error
            return 1; // Exit the program with an error code
        }
    }

    // Wait for all threads to finish
    for (int i = 0; i < num_clients; i++) {
        pthread_join(threads[i], NULL); // Join the thread with the main thread
    }

    free(threads); // Free the allocated memory for thread IDs
    return 0;       // Exit the main function
}
