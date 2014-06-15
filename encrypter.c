#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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

void handle_connection(int conn_s)
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
        close(clienttoirc[location + 1]);
        close(irctoclient[location]);
        dup2(clienttoirc[location], 0); //stdin
        dup2(irctoclient[location + 1], 1); //stdout

        execlp("openssl", "s_client", "-connect", "skynet.csh.rit.edu:6697", "-quiet", 0);
    }
    else
    {
        /* Parent process */
        close(clienttoirc[location]);
        close(irctoclient[location + 1]);
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

        printf("New pipe irc->client: %d\n", (irctoclient[location]));
    }
}

void unregister_connection(int clientnum) {
    kill(opensslpids[clientnum], SIGTERM);
    printf("Closing socket %d\n", irctoclient[clientnum * 2]);
    close(irctoclient[clientnum * 2]);
    printf("Closing socket %d\n", clients[clientnum]);
    close(clients[clientnum]);

    for(int i = clientnum; i < numConnections; i++) {
        clients[i] = clients[i + 1];
        irctoclient[i] = irctoclient[i + 2];
        irctoclient[i + 1] = irctoclient[i + 3];
        clienttoirc[i] = clienttoirc[i + 2];
        clienttoirc[i + 1] = clienttoirc[i + 3];
    }
    numConnections--;
    clients = realloc(clients, sizeof(int) * numConnections);
    irctoclient = realloc(irctoclient, sizeof(int) * numConnections * 2);
    clienttoirc = realloc(clienttoirc, sizeof(int) * numConnections * 2);

    int stat_loc;
    waitpid(opensslpids[clientnum], &stat_loc, 0);
}

int copy_data(int fd1, int fd2) {
    char buffer[BUF_SIZE];
    int conn_status;
    conn_status = read(fd1, buffer, BUF_SIZE - 1);
    if(conn_status < 0) {
        perror("read from client");
        exit(EXIT_FAILURE);
    }
    
    printf("Client -> IRC: %d bytes\n", conn_status);
    conn_status = write(fd2, buffer, conn_status);
    if(conn_status < 0) {
        perror("write: clienttoirc[i * 2 + 1]");
        exit(EXIT_FAILURE);
    }

    return conn_status;
}

void handle_event(struct epoll_event ev) {
    int fd = ev.data.fd;
    printf("Handling event from fd %d\n", fd);
    int conn_status;
    for(int i = 0; i < numConnections; i++) {
        if(fd == clients[i]) {
            conn_status = copy_data(clients[i], clienttoirc[i * 2 + 1]);
        } else if(fd == irctoclient[i * 2]) {
            conn_status = copy_data(irctoclient[i * 2], clients[i]);
        }
        if(conn_status == 0) {
            unregister_connection(i);
        }
    }
}

void epoll_loop(int list_s) {

    int nfds;
    struct epoll_event events[2];

    printf("Listening on socket\n");
    for (;;) {
        nfds = epoll_wait(epollfd, events, 2, 5000);
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
                handle_connection(conn_s);
            } else {
                handle_event(events[n]);
            } 
        }
    }
}

int main(int argc, char *argv) {
    signal(SIGPIPE, SIG_IGN); //No justice no peace

    int list_s;        //Listening socket
    short int port = INCPORT;    //port number
    struct sockaddr_in servaddr;    //socket address struct

    //Network setup
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

    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }

    //Epoll setup
    struct epoll_event ev;

    ev.events = EPOLLIN;
    ev.data.fd = list_s;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, list_s, &ev) == -1) {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    //Call loop to wait for connections
    epoll_loop(list_s);
}
