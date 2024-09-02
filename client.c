#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_NAME_LEN 15
#define MAX_MSG_LEN 127

volatile sig_atomic_t stop = 0;

void clear_previous_line() {
    printf("\033[F");  // Move cursor to the previous line
    printf("\033[K");  // Clear the line
    fflush(stdout);    // Ensure escape code is executed immediately
}

void handle_term(int signum) {
    stop = 1;
    if (signum == SIGPIPE) {
        char msg[] = "Server unexpectedly disconnected";
        write(STDERR_FILENO, msg, strlen(msg));
    }
}

void setup_signal_handlers() {
    int signals[] = {SIGTERM, SIGINT, SIGQUIT, SIGABRT, SIGILL, SIGSEGV, SIGFPE, SIGBUS, SIGTRAP, SIGCHLD};
    size_t num_signals = sizeof(signals) / sizeof(signals[0]);

    struct sigaction sa;
    sa.sa_handler = handle_term;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    for (size_t i = 0; i < num_signals; i++) {
        if (sigaction(signals[i], &sa, NULL) != 0) {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }
    }
}

// argv[1] = server ip
// argv[2] = port
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Not enough arguments\n");
        exit(1);
    }

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &addr.sin_addr);
    if (connect(sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_in)) == -1) {
        perror("connect");
        exit(1);
    }

    char msgbuf[MAX_MSG_LEN + 1];

    printf("Ready\n");
    // char name[31];
    // fgets(name, 31, stdin);

    int pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(1);
    }

    // child: message reader
    if (pid == 0) {
        FILE *reader = fdopen(sfd, "r");
        while (1) {
            if (fgets(msgbuf, MAX_MSG_LEN + 1, reader) == NULL) {
                fclose(reader);
                exit(1);
            }
            // TODO: handle case if message longer than 128?
            printf("%s", msgbuf);
        }
    }

    // parent: message writer
    while (!stop) {
        fgets(msgbuf, MAX_MSG_LEN + 1, stdin);
        clear_previous_line();
        write(sfd, msgbuf, strlen(msgbuf));
    }

    close(sfd);
    kill(pid, SIGTERM);
    wait(NULL);
    return 0;
}
