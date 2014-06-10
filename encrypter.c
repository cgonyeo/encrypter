#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <errno.h>

#define BUF_SIZE 1024
#define INCPORT 3254
#define LISTENQ (1024)

int epollfd;

int numConnections = 0;

int *irctoclient = NULL;
int *clienttoirc = NULL;
int *clients = NULL;
int *opensslpids = NULL;

void handleConnection(int conn_s)
{
    printf("New connection with fd %d\n", conn_s);
    int location = numConnections * 2;
    numConnections++;

    irctoclient = realloc(irctoclient, sizeof(int) * numConnections * 2);
    clienttoirc = realloc(clienttoirc, sizeof(int) * numConnections * 2);
    clients = realloc(clients, sizeof(int) * numConnections);
    opensslpids = realloc(opensslpids, sizeof(int) * numConnections);

    clients[numConnections - 1] = conn_s;

    pipe(irctoclient + location);
    pipe(clienttoirc + location);

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
        dup2(clienttoirc[location], 0); //stdin
        dup2(irctoclient[location + 1], 1); //stdout

        execlp("openssl", "s_client", "-connect", "skynet.csh.rit.edu:6697", "-quiet", 0);
    }
    else
    {
        /* Parent process */
        opensslpids[numConnections - 1] = childpid;

        struct epoll_event ev;

        ev.events = EPOLLIN;
        ev.data.fd = conn_s;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_s, &ev) == -1) {
            perror("epoll_ctl: listen_sock");
            exit(EXIT_FAILURE);
        }
        ev.data.fd = irctoclient[location];
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, irctoclient[location], &ev) == -1) {
            perror("epoll_ctl: listen_sock");
            exit(EXIT_FAILURE);
        }
    }
}

void handle_event(struct epoll_event ev, char *buffer) {
    int fd = ev.data.fd;
    int i, conn_status;
    for(i = 0; i < numConnections; i++) {
        if(fd == clients[i]) {
            conn_status = -1;
            if(ev.events & EPOLLIN) {
                conn_status = read(fd, buffer, BUF_SIZE - 1);
                if(conn_status < 0) {
                    perror("read from client");
                    exit(EXIT_FAILURE);
                }
                buffer[conn_status] = '\0';
                printf("Client -> IRC (%d): %s\n", conn_status, buffer);
                conn_status = write(clienttoirc[i * 2 + 1], buffer, strlen(buffer));
                if(conn_status < 0) {
                    perror("write: clienttoirc[i * 2 + 1]");
                    exit(EXIT_FAILURE);
                }
            } 
            if(conn_status == 0 || ev.events & EPOLLRDHUP || ev.events & EPOLLHUP) {
                printf("Closing socket %d\n", fd);
                if(epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) != 0) {
                    perror("epoll_ctl, deleting fd");
                    exit(EXIT_FAILURE);
                }
                if(epoll_ctl(epollfd, EPOLL_CTL_DEL, irctoclient[i * 2], NULL) != 0) {
                    perror("epoll_ctl, deleting fd");
                    exit(EXIT_FAILURE);
                }
                kill(opensslpids[i], SIGTERM);
                close(irctoclient[2 * i]);
                close(fd);
            }
        } else if(fd == irctoclient[i * 2]) {
            int conn_status = read(irctoclient[i * 2], buffer, BUF_SIZE - 1);
            if(conn_status < 0) {
                perror("read: irctoclient[0]");
                exit(EXIT_FAILURE);
            } else if(conn_status > 0) {
                buffer[conn_status] = '\0';
                printf("IRC -> Client (%d): %s\n", conn_status, buffer);
                conn_status = write(clients[i], buffer, strlen(buffer));
                if(conn_status < 0) {
                    perror("write to client");
                    exit(EXIT_FAILURE);
                }
            } else {
                printf("Closing socket %d\n", fd);
                if(epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) != 0) {
                    perror("epoll_ctl, deleting fd");
                    exit(EXIT_FAILURE);
                }
                if(epoll_ctl(epollfd, EPOLL_CTL_DEL, clients[i], NULL) != 0) {
                    perror("epoll_ctl, deleting fd");
                    exit(EXIT_FAILURE);
                }
                kill(opensslpids[i], SIGTERM);
                close(fd);
                close(clients[i]);
            }
        }
    }
}

int main(int argc, char *argv) {
    signal(SIGPIPE, SIG_IGN); //No justice no peace

    int list_s;        //Listening socket
    short int port = INCPORT;    //port number
    struct sockaddr_in servaddr;    //socket address struct
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

    if(bind(list_s, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
    {
        printf("Error calling bind\n");
        exit(EXIT_FAILURE);
    }
    if(listen(list_s, LISTENQ) < 0)
    {
        printf("Error calling listen\n");
        exit(EXIT_FAILURE);
    }

    int nfds;

    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev;

    ev.events = EPOLLIN;
    ev.data.fd = list_s;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, list_s, &ev) == -1) {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    char *buffer = malloc(sizeof(char) * BUF_SIZE);

    struct epoll_event events[2];

    printf("Listening on socket\n");
    for (;;) {
        nfds = epoll_wait(epollfd, events, 2, 1000);
        if (nfds == -1 && errno != EINTR) {
            perror("epoll_pwait");
            exit(EXIT_FAILURE);
        }
        printf("epoll_wait returned, %d fds\n", nfds);

        int n;
        for (n = 0; n < nfds; ++n) {
            int fd = events[n].data.fd;

            if(fd == list_s) {
                int conn_s = 0;
                if((conn_s = accept(list_s, NULL, NULL)) < 0)
                {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
                printf("New connection\n");
                handleConnection(conn_s);
            } else {
                handle_event(events[n], buffer);
            } 
        }
    }
}
