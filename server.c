//
// Created by Behruz Mansurov on 12/5/21.
//

#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>

#define MAX_CLIENTS_COUNT 100

/* Client structure */
typedef struct {
    struct sockaddr_in address;
    int sockfd;
    int uid;
    char name[256];
} CLIENT_INFO;

/* Receive structure */
typedef struct {
    char *message;
    ssize_t status;
} RECEIVE;

static RECEIVE read_from_socket(CLIENT_INFO *info, uint32_t length);
static int send_to_socket(CLIENT_INFO *info, uint32_t length, char *message);
int notify_all(int uid, uint32_t name_length, char *name, uint32_t body_length, char *body, uint32_t time_length,
                char *time);

/* Queue of clients information */
CLIENT_INFO *clients[MAX_CLIENTS_COUNT];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static _Atomic unsigned int client_cnt;
static int id = 0;

static void print_client_addr(struct sockaddr_in addr) {
    printf("%d.%d.%d.%d",
           addr.sin_addr.s_addr & 0xff,
           (addr.sin_addr.s_addr & 0xff00) >> 8,
           (addr.sin_addr.s_addr & 0xff0000) >> 16,
           (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

/* Add clients to queue */
void queue_add(CLIENT_INFO *client) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS_COUNT; i++) {
        if (!clients[i]) {
            clients[i] = client;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* Remove clients to queue */
void queue_remove(int uid) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS_COUNT; i++) {
        if (clients[i]) {
            if (clients[i]->uid == uid) {
                clients[i] = NULL;
                break;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// 1 3 7 15 31
/* Handle all communication with the client */
void *client_handler(void *arg) {
    client_cnt++;
    CLIENT_INFO *info = (CLIENT_INFO *) arg;

    RECEIVE name_length_char;
    RECEIVE name;
    RECEIVE body_length_char;
    RECEIVE body;
    char *time_buffer;
    int state = 0;

    while (true) {
        if ((name_length_char = read_from_socket(info, sizeof(uint32_t))).status <= 0) {
            state = 1;
            if (name_length_char.status == 0)
                goto exit_label;
            else
                goto error_label;
        }

        //TODO: check
        uint32_t name_length = (name_length_char.message[3] << 24) | ((name_length_char.message[2] & 0xFF) << 16) | ((name_length_char.message[1] & 0xFF) << 8) | (name_length_char.message[0] & 0xFF);

        if ((name = read_from_socket(info, name_length)).status <= 0) {
            state = 3;
            if (name_length_char.status == 0)
                goto exit_label;
            else
                goto error_label;
        }

        if ((body_length_char = read_from_socket(info, sizeof(uint32_t))).status <= 0) {
            state = 7;
            if (name_length_char.status == 0)
                goto exit_label;
            else
                goto error_label;
        }

        //TODO: check
        uint32_t body_length = (body_length_char.message[3] << 24) | ((body_length_char.message[2] & 0xFF) << 16) | ((body_length_char.message[1] & 0xFF) << 8) | (body_length_char.message[0] & 0xFF);

        if ((body = read_from_socket(info, body_length)).status <= 0) {
            state = 15;
            if (name_length_char.status == 0)
                goto exit_label;
            else
                goto error_label;
        }

        time_t t = time(NULL);
        struct tm *lt = localtime(&t);
        time_buffer = malloc(32);
        state = 31;
        sprintf(time_buffer, "[%d:%d:%d]", lt->tm_hour, lt->tm_min, lt->tm_sec);

        notify_all(info->uid, name_length, name.message, body_length, body.message, 32, time_buffer);

        free(time_buffer);
        free(body.message);
        free(body_length_char.message);
        free(name.message);
        free(name_length_char.message);
        state = 0;
    }

    error_label:
        fprintf(stderr, " [ERROR] CLIENT ID: %d Something went wrong while reading\n", info->uid);
    exit_label:
        fprintf(stderr, "[SUCCESS]  CLIENT ID: %d Client has left\n", info->uid);

    close(info->sockfd);
    queue_remove(info->uid);
    free(info);

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

static RECEIVE read_from_socket(CLIENT_INFO *info, uint32_t length) {
    char *buffer = malloc(length);
    ssize_t size = length;
    ssize_t res;
    do {
        if ((res = recv(info->sockfd, buffer + (length - size), size, 0)) <= 0) {
            fprintf(stderr, "[ERROR] CLIENT ID: %d ", info->uid);
            perror("Couldn't read from socket or client shutdown");
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

int notify_all(int uid, uint32_t name_length, char *name, uint32_t body_length, char *body, uint32_t time_length,
                char *time) {
    int status;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS_COUNT; i++) {
        if (clients[i]) {
            if (clients[i]->uid != uid) {
                if ((status = send_to_socket(clients[i], sizeof(name_length), (char *) &name_length)) != 0)
                    break;
                if ((status = send_to_socket(clients[i], name_length, name)) != 0)
                    break;
                if ((status = send_to_socket(clients[i], sizeof(body_length), (char *) &body_length)) != 0)
                    break;
                if ((status = send_to_socket(clients[i], body_length, body)) != 0)
                    break;
                if ((status = send_to_socket(clients[i], sizeof(time_length), (char *) &time_length)) != 0)
                    break;
                if ((status = send_to_socket(clients[i], time_length, time)) != 0)
                    break;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return status;
}

static int send_to_socket(CLIENT_INFO *info, uint32_t length, char *message) {
    ssize_t size = length;
    ssize_t res;
    do {
        if ((res = send(info->sockfd, message + (length - size), size, 0)) <= 0) {
            fprintf(stderr, "[ERROR] CLIENT ID: %d ", info->uid);
            perror("Couldn't send to socket");
            return -1;
        }
        size = size - res;
    } while (size != 0);

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "[ERROR] The port was not specified\n");
        return EXIT_FAILURE;
    }

    //const int port = atoi(argv[1]);

    char *b_port = argv[1];

    /* Socket settings */
    struct addrinfo hints = {
            .ai_family = PF_INET,
            .ai_socktype = SOCK_STREAM,
            .ai_flags = AI_PASSIVE
    };

    struct addrinfo *result = NULL;
    int res;
    if ((res = getaddrinfo(NULL, b_port, &hints, &result)) != 0) {
        fprintf(stderr, "[ERROR] Something went wrong with getaddrinfo %s\n:", gai_strerror(res));
        return EXIT_FAILURE;
    }

    int sockfd = -1;
    for (struct addrinfo *p = result; p; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("[ERROR] Socket");
            continue;
        }

        /* Bind */
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            perror("[ERROR] Bind");
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

    /* Listen */
    if (listen(sockfd, 5) == -1) {
        perror("[ERROR] Listen");
        close(sockfd);
        return EXIT_FAILURE;
    }

    while (true) {
        struct sockaddr_in addr;
        unsigned int size = sizeof(addr);
        int connfd = accept(sockfd, (struct sockaddr *) &addr, &size);
        if (connfd == -1) {
            perror("[ERROR] Accept");
            continue;
        }

        /* Check if max clients is reached */
        if ((client_cnt + 1) == MAX_CLIENTS_COUNT) {
            fprintf(stderr, "[ERROR] Max clients reached. Rejected: ");
            print_client_addr(addr);
            printf(":%d\n", addr.sin_port);
            close(connfd);
            continue;
        }

        /* Client settings */
        CLIENT_INFO *client_info = (CLIENT_INFO *) malloc(sizeof(CLIENT_INFO));
        client_info->address = addr;
        client_info->sockfd = connfd;
        client_info->uid = id++;

        print_client_addr(addr);
        printf(":%d\n", addr.sin_port);
        /* Add client to the queue and fork thread */
        queue_add(client_info);

        /* Thread settings */
        pthread_attr_t tattr;
        pthread_attr_init(&tattr);
        pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
        /* Service client in new thread */
        pthread_t tid;
        pthread_create(&tid, &tattr, client_handler, (void *) client_info);

        /* Reduce CPU usage */
        sleep(1);
    }
}

//    uint16_t name_length;
//    int res;
//    if ((res = recv(info->sockfd, &name_length, sizeof(name_length), 0)) <= 0) {
//        fprintf(stderr, "Couldn't read name length\n");
//        //TODO: leave flag
//    }
//
//    if (res != sizeof(name_length)) {
//        fprintf(stderr, "Сouldn't read all the length\n");
//        //TODO: leave flag
//    }
