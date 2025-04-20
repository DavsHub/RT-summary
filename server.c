
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


#include <sys/stat.h>
#include <sys/types.h>


#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <signal.h>
#include <pthread.h>

#define MAX_CLIENTS 100
#define BUFFER 1024


typedef struct{
    pthread_t id;
    int active;
    char username[BUFFER];
    int fd_in;
    int fd_out;
}client_t; 

client_t clientes[MAX_CLIENTS];

int n_clients=0;
pthread_mutex_t clientes_mutex;
int running = 1;

void * handle_client(void * index);
void sigusr2_handler(int sig);


void init_clients(client_t * clients, int n) {
    for (int i = 0; i<n; i++) {
        clients[i].active = -1;
        clients[i].fd_in = -1;
        clients[i].fd_out = -1;
    }
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

void configure_sigusr2_handler() {
    struct sigaction hand;
    sigset_t mask;
    sigemptyset(&mask);
    hand.sa_mask = mask;
    hand.sa_flags = 0;
    hand.sa_handler = sigusr2_handler;
    sigaction(SIGUSR2, &hand, NULL);
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
        //printf("waiting message\n");
        if (poll(&pfd,1,timeout)>0) {
            int bytes_read = read(fd, ((char*)&msg_len) + (sizeof(msg_len) - bytes_rem), bytes_rem);
            if (bytes_read <= 0) return bytes_read;
            bytes_rem -= bytes_read;
            //printf("received %d bytes\n", bytes_read);
        } else {
            return -1;
        }
    }
    msg_len = ntohl(msg_len);
    if (msg_len<=0 || msg_len >BUFFER) return -1;

    bytes_rem = msg_len;
    while (bytes_rem>0){
        if (poll(&pfd,1,timeout)>0) {
            int bytes_read = read(fd, text + (msg_len - bytes_rem), bytes_rem);
            if (bytes_read <= 0) return bytes_read;
            bytes_rem -= bytes_read;
        } else {
            return -1;
        }
    }
    return msg_len;
}

int ready_client(int p) {
    return clientes[p].fd_out != -1 && clientes[p].fd_in != -1 && clientes[p].active <= 0;
}

void handle_socket_connection(int fd, int is_recv) {
    char username[BUFFER];
    int length = read_msg(fd, username,1000);
    if ( length <= 0) {
        perror("error en recepcion de usuario\n");
        close(fd);
        return;
    }
    username[length] = '\0';

    int p = find_username(username);
    if (p==-1 && n_clients<MAX_CLIENTS) {  // new client
        client_t * cl = &clientes[n_clients];
        pthread_mutex_lock(&clientes_mutex);
        strcpy(cl->username, username);
        if (is_recv) cl->fd_in = fd;
        else cl->fd_out = fd;
        n_clients++;
        pthread_mutex_unlock(&clientes_mutex);
    } else if (p >= 0){  // add socket to existing client
        
        pthread_mutex_lock(&clientes_mutex);
        if (is_recv) clientes[p].fd_in = fd;
        else clientes[p].fd_out = fd;
        if (ready_client(p) ) {
            pthread_create(&(clientes[p].id),NULL,handle_client, (void *) &clientes[p]);
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

    int net_length = htonl(length);  // Convert to network byte order
    memcpy(buffer, &net_length, sizeof(int));
    memcpy(buffer + sizeof(int), msg, length);
    int bytes_sent = send(fd, buffer, sizeof(int) + length, 0);
    if (bytes_sent != length+sizeof(int)) {
        perror("No se ha podido enviar el mensaje atómicamente\n");
    }
    return bytes_sent;
}
void broadcast_message(client_t* orig, const char* msg, int length) {

    for (int i=0;i<MAX_CLIENTS; i++){
        if (clientes[i].active > 0 && &clientes[i] != orig){
                send_msg(clientes[i].fd_out, msg, length);
            }
    }
}

void disconnect_client(client_t * cl){
    printf("%s se desconecta\n", cl->username);
    pthread_mutex_lock(&clientes_mutex);
    if (cl->fd_in>0) {
        close(cl->fd_in);
        cl->fd_in = -1;
    }
    if (cl->fd_out>0) {
        close(cl->fd_out);
        cl->fd_out = -1;
    }
    cl->active = 0;
    *cl->username = '\0';
    n_clients--;
    pthread_mutex_unlock(&clientes_mutex);
}

void * handle_client(void * arg){
    client_t * cl = (client_t *) arg;
    pthread_mutex_lock(&clientes_mutex);
    cl->active = 1;
    pthread_mutex_unlock(&clientes_mutex);
    printf("Nuevo cliente: %s\n", cl->username);
    while (running){
        char msg[BUFFER];
        int msg_len = read_msg(cl->fd_in, msg, 2000);
        if (msg_len==0) {
            disconnect_client(cl);
            pthread_exit(0);
        }
        if (msg_len>0) {
            char msg2[2*BUFFER+1];
            sprintf(msg2, "%s: %.*s", cl->username, msg_len, msg);
            printf("%s\n",msg2);
            broadcast_message(cl, msg2,strlen(msg2));
        }
    }
    close(cl->fd_in);
    close(cl->fd_out);
    pthread_exit(0);
}

int main(int argc, char** argv) {

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port-in> <port-out>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    configure_sigusr2_handler();
    init_clients(clientes, MAX_CLIENTS);
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
            int fd_in = accept(socket_in, (struct sockaddr*) &sa_r, &addr_len);
            if (fd_in >= 0) handle_socket_connection(fd_in,1);

            int fd_out = accept(socket_out, (struct sockaddr*) &sa_r, &addr_len);
            if (fd_out >= 0) handle_socket_connection(fd_out,0);

        }
    }
    printf("Cerrando servidor\n");
    close(socket_in);
    close(socket_out);
    for (int i=0;i<MAX_CLIENTS; i++){
        if (clientes[i].active != -1) pthread_join(clientes[i].id, NULL);
        if (clientes[i].fd_in != -1) close(clientes[i].fd_in);
        if (clientes[i].fd_out != -1) close(clientes[i].fd_out);
    }
    return (EXIT_SUCCESS);
}

