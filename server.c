
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
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>

#define MAX_CLIENTS 100
#define BUFFER 1024
#define SHM "/chat_shm"
#define SEM "/chat_sem"



typedef struct{
    char username[BUFFER];
    int fd_in;
    int fd_out;
}client_t; 

client_t clientes[MAX_CLIENTS];
sem_t* sem;
int running = 1;


void configure_sigusr2_handler() {
    struct sigaction hand;
    sigset_t mask;
    sigemptyset(&mask);
    hand.sa_mask = mask;
    hand.sa_flags = 0;
    hand.sa_handler = sigusr2_handler;
    sigaction(SIGINT, &hand, NULL);
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

    struct sockaddr_in sa_r;
    socklen_t const addr_len = sizeof(sa_r);
    while(1) {
        int fd_in = accept(socket_in, (struct sockaddr*) &sa_r, &addr_len);
        int fd_out = accept(socket_out, (struct sockaddr*) &sa_r, &addr_len);
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

