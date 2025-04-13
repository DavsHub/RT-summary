#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#define BUFFER 1024

int in_add, out_add;
volatile sig_atomic_t running = 1;

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

int connect_socket(struct sockaddr_in * sa, char* username) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0); //tcp socket
    if (sfd < 0) {
        perror("Error creating socket");
        return -1;
    }
    if(connect(sfd, (struct sockaddr*) sa, sizeof(sa))<0) {
        perror("Error connecting");
        return -1;
    }

    send_msg(sfd,username, strlen(username));

    return sfd;
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

void* reader(void* arg){
    int len;
    char buf[BUFFER];
    while (running & read(out_add,&len,sizeof(int))>0){
        if (read(out_add,buf,len)<=0) break;
        buf[len] = '\0';
        printf("%s\n",buf);
    }
    return NULL;
}

void* writer(void* arg){
    char msg[BUFFER];
    while (running){
        fgets(msg,BUFFER,stdin);
        msg[strcspn(msg,"\n")] = '\0';
        if (strlen(msg)) send_with_len(in_add,msg);
    }
    return NULL;
}

int main(int argc, char* argv[]){
    if (argc != 5){
        fprintf(stderr, "Usage: %s <username> <server-ip> <port-in> <port-out>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    configure_sigusr1_handler();

    struct sockaddr_in sa_r; // IPv4
    memset(&sa_r, 0, sizeof(sa_r));


    inet_pton(AF_INET, argv[2], &(sa_r.sin_addr));
    sa_r.sin_family = AF_INET;
    sa_r.sin_port = htons(atoi(argv[3]));

    int socket_in = connect_socket(&sa_r, argv[1]);

    sa_r.sin_port = htons(atoi(argv[4]));
    int socket_out = connect_socket(&sa_r, argv[1]);

    return 0;

}