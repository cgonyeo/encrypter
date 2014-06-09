#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUF_SIZE 1024
#define INCPORT 3254
#define LISTENQ (1024)

void handleConnection(int conn_s)
{
    int irctoclient[2];
    int clienttoirc[2];
    pipe(irctoclient);
    pipe(clienttoirc);

    int childpid;

    printf("Executing openssl\n");
    if((childpid = fork()) == -1)
    {
        perror("fork");
        exit(1);
    }

    if(childpid == 0)
    {

        /* Child process */
        dup2(clienttoirc[0], 0); //stdin
        dup2(irctoclient[1], 1); //stdout

        execlp("openssl", "s_client", "-connect", "skynet.csh.rit.edu:6697", "-quiet", 0);
    }
    else
    {
        /* Parent process */
        char *buffer = malloc(sizeof(char) * BUF_SIZE);

        int n;

        struct epoll_event ev1, ev2, events[2];
        int nfds, epollfd;

        epollfd = epoll_create(10);
        if (epollfd == -1) {
            perror("epoll_create");
            exit(EXIT_FAILURE);
        }

        ev1.events = EPOLLIN;
        ev1.data.fd = conn_s;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_s, &ev1) == -1) {
            perror("epoll_ctl: listen_sock");
            exit(EXIT_FAILURE);
        }
        ev2.events = EPOLLIN;
        ev2.data.fd = irctoclient[0];
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, irctoclient[0], &ev2) == -1) {
            perror("epoll_ctl: listen_sock");
            exit(EXIT_FAILURE);
        }

        for (;;) {
            nfds = epoll_wait(epollfd, events, 2, 1000);
            if (nfds == -1) {
                perror("epoll_pwait");
                exit(EXIT_FAILURE);
            }
            printf("epoll_wait returned, %d fds\n", nfds);

            for (n = 0; n < nfds; ++n) {
                int fd = events[n].data.fd;

                if(fd == conn_s) {
                    int conn_status = read(conn_s, buffer, BUF_SIZE - 1);
                    if(conn_status < 0) {
                        perror("read: conn_s");
                        exit(EXIT_FAILURE);
                    }
                    buffer[conn_status] = '\0';
                    printf("Client -> IRC (%d): %s\n", conn_status, buffer);
                    conn_status = write(clienttoirc[1], buffer, strlen(buffer));
                    if(conn_status < 0) {
                        perror("write: clienttoirc[1]");
                        exit(EXIT_FAILURE);
                    }
                } else if(fd == irctoclient[0]) {
                    int conn_status = read(irctoclient[0], buffer, BUF_SIZE - 1);
                    if(conn_status < 0) {
                        perror("read: irctoclient[0]");
                        exit(EXIT_FAILURE);
                    }
                    buffer[conn_status] = '\0';
                    printf("IRC -> Client (%d): %s\n", conn_status, buffer);
                    conn_status = write(conn_s, buffer, strlen(buffer));
                    if(conn_status < 0) {
                        perror("write: conn_s");
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }
    }
}

int main(int argc, char *argv) {
    int list_s;        //Listening socket
    int conn_s;        //connection socket
    short int port = INCPORT;    //port number
    struct sockaddr_in servaddr;    //socket address struct
    printf("Starting socket\n");
    if((list_s = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    {
        printf("Error making listening socket\n");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("0.0.0.0");
    servaddr.sin_port = htons(port);

    int yes = 1;

    if(setsockopt(list_s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
        printf("Error setting socket options\n");
        exit(EXIT_FAILURE);
    }

    printf("Binding to socket\n");
    if(bind(list_s, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
    {
        printf("Error calling bind\n");
        exit(EXIT_FAILURE);
    }
    printf("Setting socket to listen\n");
    if(listen(list_s, LISTENQ) < 0)
    {
        printf("Error calling listen\n");
        exit(EXIT_FAILURE);
    }
    while(1)
    {
        printf("Accepting on socket\n");
        if((conn_s = accept(list_s, NULL, NULL)) < 0)
        {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        printf("New connection\n");

        handleConnection(conn_s);
    }
}
