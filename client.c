#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>

#define BUFFER 1024

int in_add, out_add;
volatile sig_atomic_t running = 1;

void sigusr1_handler(int sig){
    running=0;
}

void send_with_len(int fd, const char* msg){
    int len = strlen(msg);
    write(fd,&len,sizeof(int));
    write(fd,msg,len);
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
    if (argc != 2){
        fprintf(stderr, "Uso: %s username\n", argv[0]);
        return 1;
    }
    
    signal(SIGUSR1, sigusr1_handler);
    
    char in_pipe[BUFFER], out_pipe[BUFFER];
    snprintf(in_pipe, sizeof(in_pipe),"/tmp/%s-in",argv[1]);
    snprintf(out_pipe, sizeof(out_pipe),"/tmp/%s-out",argv[1]);
    
    mkfifo(in_pipe,0666);
    mkfifo(out_pipe,0666);

    printf("%s %s %s\n",argv[1], in_pipe, out_pipe);
    fflush(stdout);
    
    in_add = open(in_pipe,O_WRONLY);
    out_add = open(out_pipe,O_RDONLY);
    
    pthread_t t_read, t_write;
    pthread_create(&t_read, NULL, reader, NULL);
    pthread_create(&t_write, NULL, writer, NULL);
    
    pthread_join(t_read, NULL);
    pthread_join(t_write, NULL);
    
    close(in_add);
    close(out_add);
    unlink(in_pipe);
    unlink(out_pipe);
    return 0;

}