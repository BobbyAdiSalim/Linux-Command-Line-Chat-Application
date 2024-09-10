#define MAX_NAME_LEN 15
#define MAX_MSG_LEN 127
#define MAX_EVENTS 16
#define MAX_CLIENTS 16

typedef struct client {
    int fd;
    FILE *f;
    char name[MAX_NAME_LEN + 1];
} client;

typedef struct client_node {
    client *cl;
    struct client_node *next;
} client_node;

client_node *add_client(client_node *head, client *cl);

client_node *remove_client(client_node *head, int fd);

client_node *connect_client(client_node *head, int efd, int cfd);

client_node *disconnect_client(client_node *head, int efd, client *cl);

int send_to_clients(client_node **head, int efd, char *msg, int len);

int recv_from_client(client_node **head, int efd, client *cl, char *dest, int len);
