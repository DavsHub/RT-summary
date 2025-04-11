
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
sem_t* sem;
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

int recv_username(int fd, char * username, int timeout) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    int char_len;
    if (poll(&pfd,1,timeout)>0) {
        if (read(fd, &char_len, sizeof(char_len)) == -1) return -1;
    } else return -1;

    if (char_len<=0 || char_len >BUFFER) return -1;

    if (poll(&pfd,1,timeout)>0) {
        read(fd, username, sizeof(char)*char_len);
    } else return -1;
    return 0;
}

void handle_socket_connection(int fd, int is_recv) {
    char username[BUFFER];
    if (recv_username(fd, username,1000) == -1) return;

    int p = find_username(username);
    if (p==-1 && n_clients<MAX_CLIENTS) {
        client_t * cl = &clientes[n_clients];
        strcpy(cl->username, username);
        if (is_recv) cl->fd_in = fd;
        else cl->fd_out = fd;
        pthread_create(&cl->id,NULL,handle_client, n_clients);
        n_clients++;
    } else if (p >= 0){
        if (is_recv) clientes[p].fd_in = fd;
        else clientes[p].fd_out = fd;
        
    } else {
        fprintf(stderr, "Se ha alcanzado el número máximo de clientes\n");
    }
}

void send_message(int sender_index, const char* msg, int len){
    // COGE EL MENSAJE DEL CLIENTE, Y MANDA EL MENSAJE A LOS
    // OTROS CLIENTES; CON UN  SEMAFORO PROTEGE EL ACCESO AL VECTOR
    // COMPARTIDO
    sem_wait(sem);
    for (int i=0;i<MAX_CLIENTS; i++){
        if (clients[i].active && i != sender_index){
            char full_msg[BUFFER];
            int written = snprintf(full_msg, BUFFER, "%s: %.*s",clients[sender_index].username,len,msg);
            if (written < 0 || written >= BUFFER){
                fprintf(stderr, "Error en formato.\n");
                continue;
            }
          
            write(clients[i].out_add,&written,sizeof(int));
            write(clients[i].out_add,full_msg,written);
        }
    }
    sem_post(sem);
}



void handle_client(int index){
    // COGE LOS MENSAJES DEL CLIENTE Y LOS REENVIA, 
    // CUANDO TERMINA DESCONECTA EL CLIENTE,
    // CIERRA LOS PIPES Y FINALIZA EL PROCESO HIJO
    int fd = clients[index].in_add;
    int len;
    char msg[BUFFER];
    while (read(fd,&len,sizeof(int))>0){
        if (read(fd,msg,len)<=0) break;
        msg[len] = '\0';
        send_message(index,msg,len);
    }
    
    sem_wait(sem);
    clients[index].active=0;
    sem_post(sem);
    
    close(clients[index].in_add);
    close(clients[index].out_add);
    unlink(clients[index].in_pipe);
    unlink(clients[index].out_pipe);
    exit(0);
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

    //fcntl(socket_in, F_SETFL, O_NONBLOCK);
    //fcntl(socket_out, F_SETFL, O_NONBLOCK);

    struct sockaddr_in sa_r;
    socklen_t addr_len = sizeof(sa_r);
    while(1) {
        if (poll(fds,2,-1)>0) {
            int fd_in = accept(socket_in, (struct sockaddr*) &sa_r, &addr_len);
            if (fd_in >= 0) handle_socket_connection(fd_in,1);

            int fd_out = accept(socket_out, (struct sockaddr*) &sa_r, &addr_len);
            if (fd_out >= 0) handle_socket_connection(fd_out,0);

        }
    }
    
    while (running){
        char uname[BUFFER],in_pipe[BUFFER],out_pipe[BUFFER];
        scanf("%s %s %s",uname,in_pipe,out_pipe);
        int in_add=open(in_pipe, O_RDONLY);
        int out_add=open(out_pipe, O_WRONLY);
        if (in_add < 0 || out_add < 0) continue;
        
        int index =-1;
        sem_wait(sem);
        for (int i=0; i<MAX_CLIENTS; ++i){
            if (!clients[i].active){
                clients[i].in_add = in_add;
                clients[i].out_add = out_add;
                strcpy(clients[i].username,uname);
                strcpy(clients[i].in_pipe,in_pipe);
                strcpy(clients[i].out_pipe,out_pipe);
                clients[i].active=1;
                index=1;
                break;
            }
        }
        sem_post(sem);
        if (index != -1 && fork()== 0){
            handle_client(index);
        }
    }
    shm_unlink(SHM);
    sem_unlink(SEM);

    return (EXIT_SUCCESS);
}

