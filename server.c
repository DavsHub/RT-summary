/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

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


#define MAX_CLIENTS 100
#define BUFFER 1024
#define SHM "/chat_shm"
#define SEM "/chat_sem"


typedef struct{
    int in_add;
    int out_add;
    char username[BUFFER];
    int active;
    char in_pipe[BUFFER];
    char out_pipe[BUFFER];
}client_t;

client_t* clients;  
sem_t* sem;
int running = 1;

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
    signal(SIGUSR2,sigusr2_handler);
    int shm_fd=shm_open(SHM, O_CREAT | O_RDWR, 0666);
    ftruncate (shm_fd,MAX_CLIENTS*sizeof(client_t));
    clients=mmap(NULL,MAX_CLIENTS*sizeof(client_t),PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd,0);
    sem = sem_open(SEM,O_CREAT,0666,1);
    printf("Server active, waiting for clients...\n");
    
    
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

