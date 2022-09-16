#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#include "common.h"

#define BUFFER_SIZE 2048

void my_recvfrom(int sockfd, void *buf, int *len, struct sockaddr *from, socklen_t *fromlen, int* source, int* type)
{
    char buffer[BUFFER_SIZE];
    int recv_bytes = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, from, fromlen);
    if (recv_bytes < 0)
    {
        perror("receive error");
        exit(EXIT_FAILURE);
    }
    buffer[recv_bytes] = '\0';

    my_header *h = (my_header *)buffer;
    *len = h->length;
    *source = h->source;
    *type = h->type;

    memcpy(buf, buffer+sizeof(my_header), h->length);
}

void my_sendto(int sockfd, const void *msg, int len, int source, int type, const struct sockaddr *to, socklen_t tolen)
{
    if(len == 0)
    {
        my_header header;
        header.source = source;
        header.type = type;
        header.length = 0;

        int sent_bytes = sendto(sockfd, (void*)&header, sizeof(my_header), 0, to, tolen);
        if (sent_bytes != sizeof(struct MY_header))
        {
            perror("send error len=0");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        char buffer[BUFFER_SIZE];
        my_header* h = (my_header*)buffer;
        h->source = source;
        h->type = type;
        h->length = len;
        memcpy((void *)buffer+sizeof(my_header), msg, len);

        int sent_bytes = sendto(sockfd, (void*)buffer, sizeof(my_header) + len, 0, to, tolen);
        if (sent_bytes != (sizeof(struct MY_header) + len))
        {
            perror("send error");
            exit(EXIT_FAILURE);
        }
    }
}
