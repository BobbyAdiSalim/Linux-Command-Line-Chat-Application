#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include "server.h"

int num_cls = 0;
char name = 'a';
msg_queue msg_cache = { .head = NULL, .tail = NULL, .count = 0, .max = MAX_MSG_QUEUE };

client_node *add_client(client_node *head, client *cl) {
    client_node *new_cl_node = malloc(sizeof(client_node));
    new_cl_node->cl = cl;
    new_cl_node->next = head;
    return new_cl_node;
}

client_node *remove_client(client_node *head, int fd) {
    if (head == NULL)
        return head;
    if (head->cl->fd == fd) {
        fclose(head->cl->f);
        free(head->cl);
        client_node *new_head = head->next;
        free(head);
        return new_head;
    }

    client_node *cur;
    for (cur = head; 
            cur->next != NULL && cur->next->cl->fd != fd; 
            cur = cur->next);

    client_node *temp = cur->next;
    if (temp == NULL)
        return head;

    fclose(temp->cl->f);
    free(temp->cl);
    cur->next = temp->next;
    free(temp);
    return head;
}

client_node *connect_client(client_node *head, int efd, int cfd) {
    if (num_cls >= MAX_CLIENTS) {
        char msg = 0;
        send(cfd, &msg, sizeof(msg), MSG_DONTWAIT);
        close(cfd);
        return head;
    }
    struct epoll_event ev;
    client *cl = malloc(sizeof(client));
    cl->fd = cfd;
    cl->f = fdopen(cfd, "r");
    memset(cl->name, 0, MAX_NAME_LEN + 1);
    ev.events = EPOLLIN;
    ev.data.ptr = (void *) cl;
    epoll_ctl(efd, EPOLL_CTL_ADD, cfd, &ev);
    char msg = 1;
    send(cfd, &msg, sizeof(msg), MSG_DONTWAIT);
    printf("New client connected; client count = %d\n", ++num_cls);
    return add_client(head, cl);
}

client_node *disconnect_client(client_node *head, int efd, client *cl) {
    epoll_ctl(efd, EPOLL_CTL_DEL, cl->fd, NULL);
    char msg[40];
    sprintf(msg, "</%s left the chat :(/>\n", cl->name);
    printf("%s disconnected; client count = %d\n", cl->name, --num_cls);
    head = remove_client(head, cl->fd);
    send_to_clients(&head, efd, msg, strlen(msg));
    return head;
}

int send_to_clients(client_node **head_addr, int efd, char *msg, int len) {
    push_msg(&msg_cache, msg);
    int count = 0;
    for (client_node *node = *head_addr; node != NULL; node = node->next) {
        if (node->cl->name[0] != '\0') send(node->cl->fd, msg, len, MSG_DONTWAIT);
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            *head_addr = disconnect_client(*head_addr, efd, node->cl);
            count++;
        }
    }
    return count;
}

int recv_from_client(client_node **head_addr, int efd, client *cl, char *dest, int len) {
    if (fgets(dest, len, cl->f) == NULL || dest[strlen(dest) - 1] != '\n') {
        *head_addr = disconnect_client(*head_addr, efd, cl);
        return 1;
    }
    return 0;
}

void push_msg(msg_queue *queue, char *msg) {
    while (queue->count >= queue->max) {
        pop_msg(queue);
    }
    msg_node *new_node = malloc(sizeof(msg_node));
    strcpy(new_node->msg, msg);
    new_node->prev = queue->head;
    new_node->next = NULL;
    if (queue->head != NULL) {
        queue->head->next = new_node;
    }
    queue->head = new_node;
    if (queue->count++ == 0) {
        queue->tail = new_node;
    }
}

void pop_msg(msg_queue *queue) {
    if (queue == NULL || queue->tail == NULL) {
        return;
    }
    msg_node *temp = queue->tail;
    if (queue->tail->next != NULL) {
        queue->tail->next->prev = NULL;
    } else {
        queue->head = NULL;
    }
    queue->tail = queue->tail->next;
    free(temp);
    queue->count--;
}

void handle_msg(client_node **head_addr, int efd, client *cl, char *msg) {
    char sendbuf[SENDBUF_LEN];
    
    // regular message
    if (*msg != '/') {
        sprintf(sendbuf, "%s: %s", cl->name, msg);
        send_to_clients(head_addr, efd, sendbuf, strlen(sendbuf));
        return;
    }
    char cmd[16];
    char arg[16];
    int idx;
    sscanf(msg, "/%15s %15s %n", cmd, arg, &idx);
    
    // whisper
    if (strcmp(cmd, "whisper") == 0) {
        client_node *cur;
        for (cur = *head_addr; cur != NULL && 
                strcmp(cur->cl->name, arg); cur = cur->next);
        
        if (cur == NULL) {
            send(cl->fd, "</Name not found/>\n", 19, MSG_DONTWAIT);
            return;
        }

        sprintf(sendbuf, "%s whispers to you: %s", cl->name, &msg[idx]);
        send(cur->cl->fd, sendbuf, strlen(sendbuf), MSG_DONTWAIT);
        sprintf(sendbuf, "You whisper to %s: %s", cur->cl->name, &msg[idx]);
        send(cl->fd, sendbuf, strlen(sendbuf), MSG_DONTWAIT);
        return;
    }

    // quit
    if (strcmp(cmd, "quit") == 0) {
        *head_addr = disconnect_client(*head_addr, efd, cl);
    }

    // Invalid command
    send(cl->fd, "</Invalid command/>\n", 20, MSG_DONTWAIT);
    return;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Not enough arguments\n");
        exit(1);
    }

    signal(SIGPIPE, SIG_IGN);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[1]));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int lfd = socket(AF_INET, SOCK_STREAM, 0);

    if (bind(lfd, (struct sockaddr *) &addr, 
                sizeof(struct sockaddr_in)) == -1) {
        perror("bind");
        exit(1);
    }

    if (listen(lfd, 2) == -1) {
        perror("listen");
        exit(1);
    }

    int efd = epoll_create1(0);
    if (efd < 0) {
        perror("epoll_create1");
        close(lfd);
        exit(1);
    }

    struct epoll_event l_ev;
    client l_client;
    l_client.fd = lfd;
    memset(&l_client.name, 0, MAX_NAME_LEN + 1);
    l_ev.events = EPOLLIN;
    l_ev.data.ptr = (void *) &l_client;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, lfd, &l_ev) < 0) {
        perror("epoll_ctl");
        close(lfd);
        close(efd);
        exit(1);
    }

    struct epoll_event ready_evs[MAX_EVENTS];
    char recvbuf[RECVBUF_LEN];
    char sendbuf[SENDBUF_LEN];
    client_node *head = NULL;
    int num_cls = 0;

    while (1) {
        int num_evs = epoll_wait(efd, ready_evs, MAX_EVENTS, -1);
        for (int i = 0; i < num_evs; i++) {
            client *cl_info = (client *) ready_evs[i].data.ptr;
            if (cl_info->fd == lfd) {
                int cfd = accept(lfd, NULL, NULL);
                head = connect_client(head, efd, cfd);
                continue;
            }

            if (strlen(cl_info->name) == 0) {
                if (recv_from_client(&head, efd, cl_info, recvbuf, MAX_NAME_LEN + 2)) {
                    continue;
                }

                recvbuf[strlen(recvbuf) - 1] = '\0';
                strcpy(cl_info->name, recvbuf);
                printf("Hello %s!\n", cl_info->name);

                for (msg_node *cur = msg_cache.tail; cur != NULL; cur = cur->next) {
                    send(cl_info->fd, cur->msg, strlen(cur->msg), MSG_DONTWAIT);
                }

                sprintf(sendbuf, "</%s joined the chat!/>\n", cl_info->name);
                send_to_clients(&head, efd, sendbuf, strlen(sendbuf));

                continue;
            }

            if (recv_from_client(&head, efd, cl_info, recvbuf, MAX_MSG_LEN + 2)) {
                continue;
            }
            
            handle_msg(&head, efd, cl_info, recvbuf);
        }
    }
}
