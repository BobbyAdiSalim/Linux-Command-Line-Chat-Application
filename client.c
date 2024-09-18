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
#define BUF_LEN MAX_NAME_LEN + MAX_MSG_LEN + 3

volatile sig_atomic_t stop = 0;

void clear_previous_line() {
    printf("\033[F");  // Move cursor to the previous line
    printf("\033[K");  // Clear the line
    fflush(stdout);    // Ensure escape code is executed immediately
}

void handle_term(int signum) {
    stop = 1;
}

void setup_signal_handlers() {
    int signals[] = {SIGTERM, SIGQUIT, SIGABRT, SIGILL, SIGSEGV, SIGFPE, SIGBUS, SIGTRAP, SIGCHLD};
    size_t num_signals = sizeof(signals) / sizeof(signals[0]);

    struct sigaction sa;
    sa.sa_handler = handle_term;
    sa.sa_flags = 0;
    sigfillset(&sa.sa_mask);

    for (size_t i = 0; i < num_signals; i++) {
        if (sigaction(signals[i], &sa, NULL) != 0) {
            perror("sigaction");
            exit(1);
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

    signal(SIGINT, SIG_IGN);

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

    char msgbuf[BUF_LEN];
    memset(msgbuf, 0, BUF_LEN);

    printf("Ready\n");

    int pipe_rw[2];
    int pipe_wr[2];
    pipe(pipe_rw);
    pipe(pipe_wr);

    int pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(1);
    }

    // child: message reader
    if (pid == 0) { 
        close(pipe_rw[0]);
        close(pipe_wr[1]);
        if (read(sfd, msgbuf, 1) != 1) {
            fprintf(stderr, "Bad server\n");
            perror("read");
            exit(1);
        }
        if (msgbuf[0] == 1) {
            printf("Connected to server successfully\n");
            write(pipe_rw[1], msgbuf, 1);
            close(pipe_rw[1]);
        } else if (msgbuf[0] == 0) {
            printf("Disconnected; server full\n");
            exit(2);
        } else {
            fprintf(stderr, "Bad server\n");
            exit(1);
        }

        FILE *reader = fdopen(sfd, "r");

        read(pipe_wr[0], msgbuf, 1);
        close(pipe_wr[0]);

        while (fgets(msgbuf, BUF_LEN, reader) != NULL) {
            printf("%s", msgbuf);
        }
        if (feof(reader))
            printf("Server disconnected\n");
        if (ferror(reader))
            fprintf(stderr, "Read socket error\n");
        fclose(reader);
        exit(1);
    }

    // parent: message writer
    setup_signal_handlers();

    close(pipe_rw[1]);
    // close(pipe_wr[0]);
    read(pipe_rw[0], msgbuf, 1);
    close(pipe_rw[0]);

    msgbuf[0] = 0;
    while (strlen(msgbuf) < 2) {
        printf("Enter your name (1-15 characters long): ");
        fgets(msgbuf, MAX_NAME_LEN + 2, stdin);
    }
    write(sfd, msgbuf, strlen(msgbuf));
    write(pipe_wr[1], msgbuf, 1);
    close(pipe_wr[1]);

    while (!stop && fgets(msgbuf, BUF_LEN, stdin) != NULL) {
        clear_previous_line();
        write(sfd, msgbuf, strlen(msgbuf));
    }

    close(sfd);
    kill(pid, SIGTERM);
    wait(NULL);
    return 0;
}
