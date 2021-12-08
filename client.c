//
// Created by Behruz Mansurov on 12/7/21.
//

#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <fcntl.h>


/* Receive structure */
typedef struct {
    char *message;
    ssize_t status;
} RECEIVE;

typedef struct {
    char *name;
    int sockfd;
} SEND_HANDLE_ATTR;

pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

static int send_to_server(int sockfd, uint32_t length, char *message);
static RECEIVE read_from_server(int sockfd, uint32_t length);
int send_all_to_server(int sockfd, uint32_t name_length, char *name, uint32_t body_length, char *body);
void *receive_message_handler(void *arg);
void *send_message_handler(void *arg);
void print_message(char *name, char *body, char *time);

void flush() {
    fflush(stdin);
}

void str_trim_lf (char* arr, int length) {
    int i;
    for (i = 0; i < length; i++) { // trim \n
        if (arr[i] == '\n') {
            arr[i] = '\0';
            break;
        }
    }
}

void print_message(char *name, char *body, char *time) {
    pthread_mutex_lock(&print_mutex);
    flush();
    printf("[%s] ", name);
    printf("%s ", body);
    printf("%s ", time);
    printf("\n");
    flush();
    pthread_mutex_unlock(&print_mutex);
}

static RECEIVE read_from_server(int sockfd, uint32_t length) {
    char *buffer = (char *) malloc(length);
    bzero(buffer, length);
    ssize_t size = length;
    ssize_t res;
    do {
        if ((res = recv(sockfd, buffer + (length - size), size, 0)) <= 0) {
            perror("[ERROR] Couldn't read from socket or client shutdown");
            RECEIVE receive = {
                    .message = NULL,
                    .status = res
            };
            return receive;
        }
        size = size - res;
    } while (size != 0);

    RECEIVE receive = {
            .message = buffer,
            .status = res
    };

    return receive;
}

int send_all_to_server(int sockfd, uint32_t name_length, char *name, uint32_t body_length, char *body) {
    int status;

    if ((status = send_to_server(sockfd, sizeof(name_length), (char *) &name_length)) != 0)
        return status;
    if ((status = send_to_server(sockfd, name_length, name)) != 0)
        return status;
    if ((status = send_to_server(sockfd, sizeof(body_length), (char *) &body_length)) != 0)
        return status;
    if ((status = send_to_server(sockfd, body_length, body)) != 0)
        return status;
    return status;
}

static int send_to_server(int sockfd, uint32_t length, char *message) {
    ssize_t size = length;
    ssize_t res;
    do {
        if ((res = send(sockfd, message + (length - size), size, 0)) <= 0) {
            perror("[ERROR] Couldn't send to server");
            return -1;
        }
        size = size - res;
    } while (size != 0);

    return 0;
}

void *send_message_handler(void *arg) {
    struct timespec tw = {0,300000000};
    struct timespec tr;

    SEND_HANDLE_ATTR *attr = (SEND_HANDLE_ATTR *) arg;
    int name_length = strlen(attr->name);
    int message_length = 256;
    char *message = malloc(message_length);
    //printf("> ");
    flush();
    while (true) {
        int m = getchar();
        if (m == 'm') {
            pthread_mutex_lock(&print_mutex);
            flush();
            printf("> ");
            while (fgets(message, message_length, stdin) == NULL) {
                bzero(message, message_length);
            }

            str_trim_lf(message, 256);

            if (strcmp(message, "exit") == 0) {
                break;
            } else {
                uint32_t actual_message_length = strlen(message);
                printf("%s\n", message);
                send_all_to_server(attr->sockfd, name_length, attr->name, actual_message_length, message);
            }
            pthread_mutex_unlock(&print_mutex);
            bzero(message, message_length);
        }
        flush();
        nanosleep (&tw, &tr);
    }
    return NULL;
}

/* Handle all communication with the server */
void *receive_message_handler(void *arg) {
    int *sockfd  =  (int *) arg;
    RECEIVE name_length_char = {.message = NULL, .status = 0};
    RECEIVE name = {.message = NULL, .status = 0};
    RECEIVE body_length_char = {.message = NULL, .status = 0};
    RECEIVE body = {.message = NULL, .status = 0};
    char *time_buffer = NULL;
    int state = 0;

    while (true) {
        if ((name_length_char = read_from_server(*sockfd, sizeof(uint32_t))).status <= 0) {
            state = 1;
            if (name_length_char.status == 0)
                goto exit_label;
            else
                goto error_label;
        }

        //TODO: check
        uint32_t name_length = (name_length_char.message[3] << 24) | ((name_length_char.message[2] & 0xFF) << 16) | ((name_length_char.message[1] & 0xFF) << 8) | (name_length_char.message[0] & 0xFF);

        if ((name = read_from_server(*sockfd, name_length)).status <= 0) {
            state = 3;
            if (name_length_char.status == 0)
                goto exit_label;
            else
                goto error_label;
        }

        if ((body_length_char = read_from_server(*sockfd, sizeof(uint32_t))).status <= 0) {
            state = 7;
            if (name_length_char.status == 0)
                goto exit_label;
            else
                goto error_label;
        }

        //TODO: check
        uint32_t body_length = (body_length_char.message[3] << 24) | ((body_length_char.message[2] & 0xFF) << 16) | ((body_length_char.message[1] & 0xFF) << 8) | (body_length_char.message[0] & 0xFF);

        if ((body = read_from_server(*sockfd, body_length)).status <= 0) {
            state = 15;
            if (name_length_char.status == 0)
                goto exit_label;
            else
                goto error_label;
        }

        time_t t = time(NULL);
        struct tm *lt = localtime(&t);

        time_buffer  = malloc(32);
        state = 31;
        sprintf(time_buffer, "[%d:%d:%d]", lt->tm_hour, lt->tm_min, lt->tm_sec);

        print_message(name.message, body.message, time_buffer);

        free(name_length_char.message);
        name_length_char.message = NULL;
        name_length_char.status = 0;
        free(name.message);
        name.message = NULL;
        name.status = 0;
        free(body_length_char.message);
        body_length_char.message = NULL;
        body_length_char.status = 0;
        free(body.message);
        body.message = NULL;
        body.status = 0;
        free(time_buffer);
        time_buffer = NULL;
        state = 0;
    }

    error_label:
    fprintf(stderr, " [ERROR] Something went wrong while reading\n");
    exit_label:
    fprintf(stderr, "[SUCCESS] Server has left\n");

    if((state & 1) == 1)
        free(name_length_char.message);
    if((state & 3) == 3)
        free(name.message);
    if((state & 7) == 7)
        free(body_length_char.message);
    if((state & 15) == 15)
        free(body.message);
    if((state & 31) == 31)
        free(time_buffer);
    return NULL;
}

int main(int argc, char *argv[]) {
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    if (argc != 4) {
        fprintf(stderr, "[ERROR] The server address, port and name were not specified\n");
        return EXIT_FAILURE;
    }

    //const int port = atoi(argv[1]);

    char *b_server_addr = argv[1];
    char *b_port = argv[2];
    char *name = argv[3];
    //int name_length = strlen(name);

    /* Socket settings */
    struct addrinfo hints = {
            .ai_family = PF_INET,
            .ai_socktype = SOCK_STREAM
    };

    struct addrinfo *result = NULL;
    int res;
    if ((res = getaddrinfo(b_server_addr, b_port, &hints, &result)) != 0) {
        fprintf(stderr, "[ERROR] Something went wrong with getaddrinfo %s\n:", gai_strerror(res));
        return EXIT_FAILURE;
    }

    int sockfd = -1;
    for (struct addrinfo *p = result; p; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("[ERROR] Socket");
            continue;
        }

        /* Connect */
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            perror("[ERROR] Connect");
            close(sockfd);
            sockfd = -1;
            continue;
        }
        break;
    }

    if (sockfd == -1) {
        fprintf(stderr, "Fail");
        freeaddrinfo(result);
        return EXIT_FAILURE;
    }
    freeaddrinfo(result);

    SEND_HANDLE_ATTR attr = {.name = name, .sockfd = sockfd};

    /* Thread settings */
    pthread_attr_t tattr;
    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);

    /* Service client in new thread */
    pthread_t receive_tid;
    //pthread_t send_tid;

    pthread_create(&receive_tid, &tattr, receive_message_handler, (void *) &sockfd);
    //pthread_create(&send_tid, &tattr, send_message_handler, (void *) &attr);

    send_message_handler(&attr);

    //receive_message_handler(&sockfd);

    return 0;
}