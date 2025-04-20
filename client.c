#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdint.h>
#define BUFFER 1024

int in_add, out_add;
int running = 1;

typedef struct {
    struct sockaddr_in sa;
    char *  username;
} info_con_t;

void configure_info_con(info_con_t * info, char * username, char * ip, char * port) {
    memset(&info->sa, 0, sizeof(info->sa));
    inet_pton(AF_INET, ip, &(info->sa.sin_addr));
    info->sa.sin_family = AF_INET;
    info->sa.sin_port = htons(atoi(port));
    info->username = username;
}

void sigusr1_handler(int sig){
    running=0;
}

void configure_sigusr1_handler() {
    struct sigaction hand;
    sigset_t mask;
    sigemptyset(&mask);
    hand.sa_mask = mask;
    hand.sa_flags = 0;
    hand.sa_handler = sigusr1_handler;
    sigaction(SIGUSR1, &hand, NULL);
}

int send_msg(int fd, const char* msg, int length) {
    char buffer[sizeof(int) + length];

    int net_length = htonl(length);  // Convert to network byte order
    memcpy(buffer, &net_length, sizeof(int));
    memcpy(buffer + sizeof(int), msg, length);
    int bytes_sent = send(fd, buffer, sizeof(int) + length, 0);
    if (bytes_sent != length+sizeof(int)) {
        fprintf(stderr, "No se ha podido enviar el mensaje atómicamente\n");
        return -1;
    }
    return bytes_sent;
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
            if (bytes_read <= 0) return bytes_read;
            bytes_rem -= bytes_read;
        } else {
            return -1;
        }
    }
    msg_len = ntohl(msg_len);
    if (msg_len<=0 || msg_len >(2*BUFFER+1)) return -1;

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

int connect_socket(struct sockaddr_in * sa, char* username) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0); //tcp socket
    if (sfd < 0) {
        perror("Error creating socket");
        return -1;
    }
    if(connect(sfd, (struct sockaddr*) sa, sizeof(*sa))<0) {
        perror("Error connecting");
        return -1;
    }

    send_msg(sfd,username, strlen(username));

    return sfd;
}

void * recieve_thread (void* arg) {
    info_con_t * info = (info_con_t*) arg;
    int socket = connect_socket(&info->sa, info->username);
    if (socket <0) {
        kill(getpid(), SIGUSR1);
        close(socket);
        pthread_exit(0);
    }
    while(running) {
        char msg[(2*BUFFER+1)];
        int length = read_msg(socket, msg, 2000); // timeout in case connection is closed by sigusr1 but not by the server
        if (length==0) {
            printf("Conexion terminada\n");
            kill(getpid(), SIGUSR1);
            break;
        }
        if (length>0) {
            write(1,"\n", 1);
            write(1,msg, length);
            write(1,"\nyou:", 6);
        }
    }
    close(socket);
    pthread_exit(0);
}

void * send_thread (void* arg) {
    info_con_t * info = (info_con_t*) arg;
    int socket = connect_socket(&info->sa, info->username);
    
    if (socket <0) {
        kill(getpid(), SIGUSR1);
        close(socket);
        pthread_exit(0);
    }
    char string[BUFFER];
    int length = 0;
    write(1,"you: ", 5);
    while(running) {
        string[length++] = getchar(); // blocks until '\n' is typed
        if (string[length-1] == '\n' && length>1) {
            int bytes_send = send_msg(socket, string, length-1);
            length = 0;
            write(1,"you: ", 5);
        }
    }
    close(socket);
    pthread_exit(0);
}

int main(int argc, char* argv[]){
    if (argc != 5){
        fprintf(stderr, "Usage: %s <username> <server-ip> <port-in> <port-out>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    struct sigaction hand;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_SETMASK,&mask,NULL);

    configure_sigusr1_handler();

    info_con_t irecv, isend; // IPv4
    configure_info_con(&isend, argv[1], argv[2], argv[3]);
    configure_info_con(&irecv, argv[1], argv[2], argv[4]);

    pthread_t tid1,tid2;
    pthread_create(&tid1, NULL, send_thread, (void *) &isend);
    pthread_create(&tid2, NULL, recieve_thread, (void *) &irecv);
    

    sigfillset(&mask);
    sigdelset(&mask, SIGUSR1);
    sigsuspend(&mask);
    
    printf("\ncerrando cliente\n(presionar ENTER para continuar)\n");

    pthread_join(tid1,NULL);
    pthread_join(tid2,NULL);

    return EXIT_SUCCESS;

}