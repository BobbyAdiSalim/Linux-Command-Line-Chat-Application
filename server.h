#ifndef _SERVER_H

#define _SERVER_H
#define MAX_NAME_LEN 15
#define MAX_MSG_LEN 127
#define MAX_EVENTS 17
#define MAX_CLIENTS 16
#define MAX_MSG_QUEUE 32
#define RECVBUF_LEN MAX_MSG_LEN + 2
#define SENDBUF_LEN MAX_NAME_LEN + MAX_MSG_LEN + 4

typedef struct client {
    int fd;
    FILE *f;
    char name[MAX_NAME_LEN + 1];
} client;

typedef struct client_node {
    client *cl;
    struct client_node *next;
} client_node;

/*
 * Maybe implement this in the future?
typedef struct client_list {
    client_node *head;
    int count;
    int max;
}
*/

// push to head (new message)
// pop from tail (old message)
// next = next msg chronologically <-> going towards head
// prev = prev msg chronologically <-> going towards tail
typedef struct msg_node {
    char msg[SENDBUF_LEN];
    struct msg_node *next;
    struct msg_node *prev;
} msg_node;

typedef struct msg_queue {
    msg_node *head;
    msg_node *tail;
    int count;
    int max;
} msg_queue;

client_node *add_client(client_node *head, client *cl);

client_node *remove_client(client_node *head, int fd);

client_node *connect_client(client_node *head, int efd, int cfd);

client_node *disconnect_client(client_node *head, int efd, client *cl);

int send_to_clients(client_node **head, int efd, char *msg, int len);

int recv_from_client(client_node **head, int efd, client *cl, char *dest, int len);

void push_msg(msg_queue *queue, char *msg);

void pop_msg(msg_queue *queue);

void handle_msg(client_node **head_addr, int efd, client *cl, char *msg);

#endif
