
/* 
 * File:   main.c
 * Author: user
 *
 * Created on 10 de abril de 2025, 12:12
 */

#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/mman.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>


#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <signal.h>

#define MAX_CLIENTS 100
#define BUFFER 1024
#define SHM "/chat_shm"
#define SEM "/chat_sem"



typedef struct{
    pthread_t id;
    char username[BUFFER];
    int fd_in;
    int fd_out;
}client_t; 

client_t clientes[MAX_CLIENTS];
int n_clients=0;
pthread_mutex_t clientes_mutex;
int running = 1;


void configure_sigusr2_handler() {
    struct sigaction hand;
    sigset_t mask;
    sigemptyset(&mask);
    hand.sa_mask = mask;
    hand.sa_flags = 0;
    hand.sa_handler = sigusr2_handler;
    sigaction(SIGUSR2, &hand, NULL);
}

int create_listening_socket(int port) {
    int sockfd;
    struct sockaddr_in server_addr;

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket creation failed");
        return -1;
    }

    // Set up server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind socket
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind failed");
        close(sockfd);
        return -1;
    }

    // Start listening
    if (listen(sockfd, 5) == -1) {
        perror("listen failed");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

void sigusr2_handler(int sig){
    running=0; // RECIBIR SIGURS2 === DEJAR DE ACEPTAR CLIENTES
}

int find_username(char * username) {
    int p = -1;
    for (int i = 0; i<MAX_CLIENTS; i++) {
        if (strcmp(clientes[i].username,username)==0) {
            p=i;
            break;
        }
    }
    return p;
}

int read_msg(int fd, char * text, int timeout) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    int msg_len;
    int bytes_rem = sizeof(msg_len);
    while (bytes_rem>0){
        if (poll(&pfd,1,timeout)>0) {
            int bytes_read = read(fd, ((char*)&msg_len) + (sizeof(msg_len) - bytes_rem), bytes_rem);
            if (bytes_read <= 0) return -1;
            bytes_rem -= bytes_read;
        } else {
            return -1;
        }
    }

    if (msg_len<=0 || msg_len >BUFFER) return -1;

    bytes_rem = msg_len;
    while (bytes_rem>0){
        if (poll(&pfd,1,timeout)>0) {
            int bytes_read = read(fd, text + (msg_len - bytes_rem), bytes_rem);
            if (bytes_read <= 0) return -1;
            bytes_rem -= bytes_read;
        } else {
            return -1;
        }
    }
    return msg_len;
}

int valid_client(int p) {
    return clientes[p].fd_out > 0 && clientes[p].fd_in > 0 && clientes[p].username != NULL && clientes[p].id == 0;
}

void handle_socket_connection(int fd, int is_recv) {
    char username[BUFFER];
    if (read_msg(fd, username,1000) == -1) {
        perror("error en recepcion de usuario\n");
        close(fd);
        return;
    }

    int p = find_username(username);
    if (p==-1 && n_clients<MAX_CLIENTS) {  // new client
        client_t * cl = &clientes[n_clients];
        pthread_mutex_lock(&clientes_mutex);
        strcpy(cl->username, username);
        if (is_recv) cl->fd_in = fd;
        else cl->fd_out = fd;
        pthread_mutex_unlock(&clientes_mutex);
        n_clients++;
    } else if (p >= 0){  //
        pthread_mutex_lock(&clientes_mutex);
        if (is_recv) clientes[p].fd_in = fd;
        else clientes[p].fd_out = fd;
        if (valid_client(p) ) {
            pthread_create(&(clientes[p].id),NULL,handle_client, p);
        }
        pthread_mutex_unlock(&clientes_mutex);
    } else {
        fprintf(stderr, "Se ha alcanzado el número máximo de clientes\n");
        close(fd);
        return;
    }
}

int send_msg(int fd, const char* msg, int length) {
    char buffer[sizeof(int) + length];

    memcpy(buffer, &length, sizeof(int));
    memcpy(buffer + sizeof(int), msg, length);
    int bytes_sent = send(fd, buffer, sizeof(int) + length, 0);
}
void broadcast_message(const char* msg, int length) {

    pthread_mutex_lock(&clientes_mutex);
    for (int i=0;i<MAX_CLIENTS; i++){
        if (clientes[i].id != 0){
                send_msg(clientes[i].fd_out, msg, length);
            }
    }
    pthread_mutex_unlock(&clientes_mutex);

}



void handle_client(int index){
    int fd_in = clientes[index].fd_in;
    while (1){
        char msg[BUFFER];
        int msg_len = read_msg(fd_in, msg, -1);
        broadcast_message(msg,msg_len);
    }
}

int main(int argc, char** argv) {

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port-in> <port-out>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    configure_sigusr2_handler();

    
    int socket_in = create_listening_socket(atoi(argv[1]));
    int socket_out = create_listening_socket(atoi(argv[2]));

    struct pollfd fds[2];

    fds[0].fd = socket_in;
    fds[0].events = POLLIN;
    fds[1].fd = socket_out;
    fds[1].events = POLLIN;

    fcntl(socket_in, F_SETFL, O_NONBLOCK);
    fcntl(socket_out, F_SETFL, O_NONBLOCK);

    struct sockaddr_in sa_r;
    socklen_t addr_len = sizeof(sa_r);
    while(running) {
        if (poll(fds,2,-1)>0) {
            int fd_in = accept4(socket_in, (struct sockaddr*) &sa_r, &addr_len, SOCK_NONBLOCK);
            if (fd_in >= 0) handle_socket_connection(fd_in,1);

            int fd_out = accept4(socket_out, (struct sockaddr*) &sa_r, &addr_len, SOCK_NONBLOCK);
            if (fd_out >= 0) handle_socket_connection(fd_out,0);

        }
    }
    close(socket_in);
    close(socket_out);
    for (int i=0;i<MAX_CLIENTS; i++){
        if (clientes[i].fd_in>0) close(clientes[i].fd_in);
        if (clientes[i].fd_out>0) close(clientes[i].fd_out);
    }
    return (EXIT_SUCCESS);
}

