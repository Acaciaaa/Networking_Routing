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

#define MAX_LINE 50
#define BUFFER_SIZE 2048

int location_num = 0;
int RtoV[MAX_ID];
int VtoR[MAX_ROUTER];

struct TUPLE tuple[MAX_ROUTER];

void trigger_all(int sockfd, struct sockaddr_in receiver_addr)
{
    while(1)
    {
        for(int i = 0; i < location_num; ++i)
        {
            receiver_addr.sin_port = htons(tuple[i].port);
            // convert IPv4 or IPv6 addresses from text to binary form
            if(inet_pton(AF_INET, tuple[i].ip, &receiver_addr.sin_addr)<=0)
            {
                perror("address failed");
                exit(EXIT_FAILURE);
            }

            my_sendto(sockfd, NULL, 0, AGENT_S, 0, (struct sockaddr*)&receiver_addr, sizeof(struct sockaddr));
        }

        int count = 0;
        struct sockaddr_in sender;
        socklen_t addr_len = sizeof(struct sockaddr);
        char buf[BUFFER_SIZE];
        int len, source, type;
        for(int i = 0; i < location_num; ++i)
        {
            my_recvfrom(sockfd, buf, &len, (struct sockaddr*)&sender, &addr_len, &source, &type);
            if(type == 3)
                ++count;
        }
        if(count == location_num)
            break;
    }

    for(int i = 0; i < location_num; ++i)
        {
            receiver_addr.sin_port = htons(tuple[i].port);
            // convert IPv4 or IPv6 addresses from text to binary form
            if(inet_pton(AF_INET, tuple[i].ip, &receiver_addr.sin_addr)<=0)
            {
                perror("address failed");
                exit(EXIT_FAILURE);
            }

            my_sendto(sockfd, NULL, 0, AGENT_S, 4, (struct sockaddr*)&receiver_addr, sizeof(struct sockaddr));

            struct sockaddr_in sender;
            socklen_t addr_len = sizeof(struct sockaddr);
            char buf[BUFFER_SIZE];
            int len, source, type;
            my_recvfrom(sockfd, buf, &len, (struct sockaddr*)&sender, &addr_len, &source, &type);
            if(type != 1){
                perror("router knew end");
                exit(EXIT_FAILURE);
            }
        }
}

void parse_update(char* buf, int* a)
{
    int len = strlen(buf)-1;
    char tmp[10];
    memset(tmp, 0, 10);

    int s, j, e;
    s = j = 7;
    for(; ; ++j)
        if(buf[j] == ',')
        {
            e = j;
            break;
        }
    memcpy(tmp, buf+s, e-s);
    tmp[e-s] = '\0';
    a[0] = atoi(tmp);

    s = ++j;
    for(; ; ++j)
        if(buf[j] == ',')
        {
            e = j;
            break;
        }
    memcpy(tmp, buf+s, e-s);
    tmp[e-s] = '\0';
    a[1] = atoi(tmp);

    s = e+1;
    e = len;
    memcpy(tmp, buf+s, e-s);
    tmp[e-s] = '\0';
    a[2] = atoi(tmp);
}

void parse_onerouter(char* buf, int* a)
{
    int len = strlen(buf)-1;;
    char tmp[10];
    memset(tmp, 0, 10);

    int s, j;
    j = 0;
    for(; ; ++j)
        if(buf[j] == ':')
        {
            s = j + 1;
            break;
        }
    memcpy(tmp, buf+s, len-s);
    tmp[len-s] = '\0';
    *a = atoi(tmp);
}

void trigger_update(int sockfd, struct sockaddr_in receiver_addr, char* order)
{
    int a[3] = {0};
    parse_update(order, a);
    // printf("%d %d %d\n", a[0], a[1], a[2]);
    for(int j = 0; j < location_num; ++j)
        if(tuple[j].id == a[0])
        {
            receiver_addr.sin_port = htons(tuple[j].port);
            // convert IPv4 or IPv6 addresses from text to binary form
            if(inet_pton(AF_INET, tuple[j].ip, &receiver_addr.sin_addr)<=0)
            {
                perror("address failed");
                exit(EXIT_FAILURE);
            }
            int b[2];
            b[0] = RtoV[a[1]];
            b[1] = a[2];
            my_sendto(sockfd, (void*)b, sizeof(int)*2, AGENT_S, 1, (struct sockaddr*)&receiver_addr, sizeof(struct sockaddr));

            struct sockaddr_in sender;
            socklen_t addr_len = sizeof(struct sockaddr);
            char buf[BUFFER_SIZE];
            int len, source, type;
            my_recvfrom(sockfd, buf, &len, (struct sockaddr*)&sender, &addr_len, &source, &type);
            //printf("yes trigger_update!!!\n");
            break;
        }
}

void trigger_show(int sockfd, struct sockaddr_in receiver_addr, char* order)
{
    int r;
    parse_onerouter(order, &r);
    for(int i = 0; i < location_num; ++i)
    {
        if(tuple[i].id == r)
        {
            receiver_addr.sin_port = htons(tuple[i].port);
            // convert IPv4 or IPv6 addresses from text to binary form
            if(inet_pton(AF_INET, tuple[i].ip, &receiver_addr.sin_addr)<=0)
            {
                perror("address failed");
                exit(EXIT_FAILURE);
            }
            my_sendto(sockfd, NULL, 0, AGENT_S, 2, (struct sockaddr*)&receiver_addr, sizeof(struct sockaddr));

            struct sockaddr_in sender;
            socklen_t addr_len = sizeof(struct sockaddr);
            struct ROUTING_TABLE table[MAX_ROUTER];
            int len, source, type;
            my_recvfrom(sockfd, (void*)table, &len, (struct sockaddr*)&sender, &addr_len, &source, &type);
            if(len != sizeof(table))
            {
                perror("routing table");
                exit(EXIT_FAILURE);
            }
            // show the table
            for(int i = 1; i < MAX_ROUTER; ++i)
            {
                if(table[i].next == 0)
                    continue;
                printf("dest: %d, next: %d, cost: %d\n", VtoR[i], VtoR[table[i].next], table[i].dis);
            }
            break;
        }
    }
}

void trigger_reset(int sockfd, struct sockaddr_in receiver_addr, char* order)
{
    int r;
    parse_onerouter(order, &r);
    for(int i = 0; i < location_num; ++i)
    {
        if(tuple[i].id == r)
        {
            receiver_addr.sin_port = htons(tuple[i].port);
            // convert IPv4 or IPv6 addresses from text to binary form
            if(inet_pton(AF_INET, tuple[i].ip, &receiver_addr.sin_addr)<=0)
            {
                perror("address failed");
                exit(EXIT_FAILURE);
            }
            my_sendto(sockfd, NULL, 0, AGENT_S, 3, (struct sockaddr*)&receiver_addr, sizeof(struct sockaddr));

            struct sockaddr_in sender;
            socklen_t addr_len = sizeof(struct sockaddr);
            char buf[BUFFER_SIZE];
            int len, source, type;
            my_recvfrom(sockfd, buf, &len, (struct sockaddr*)&sender, &addr_len, &source, &type);
            //printf("yes trigger_upset!!!\n");
            break;
        }
    }
}

void give_orders(int sockfd, char* buf, struct sockaddr_in receiver_addr)
{
    switch(buf[0])
    {
    case 'd':
        trigger_all(sockfd, receiver_addr);
        break;
    case 'u':
        trigger_update(sockfd, receiver_addr, buf);
        break;
    case 's':
        trigger_show(sockfd, receiver_addr, buf);
        break;
    default:
        trigger_reset(sockfd, receiver_addr, buf);
    }
}

void run()
{
    int sock = 0;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // create receiver address
    struct sockaddr_in receiver_addr;
    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;

    char buf[MAX_LINE];
    while(fgets(buf, MAX_LINE, stdin) != NULL)
        give_orders(sock, buf, receiver_addr);

    close(sock);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    char *router_file;

    if (argc != 2)
    {
        fprintf(stderr, "Usage: ./agent <router location file>");
        exit(EXIT_FAILURE);
    }

    router_file = argv[1];

    // init
    memset(RtoV, 0, sizeof(int)*MAX_ID);
    memset(VtoR, 0, sizeof(int)*MAX_ROUTER);
    for(int i = 0; i < MAX_ROUTER; ++i)
    {
        memset(tuple[i].ip, 0, IP_LEN);
        tuple[i].id = 0;
        tuple[i].port = 0;
    }

    // parse location
    FILE* f = fopen(router_file, "r");
    if(f == NULL)
    {
        perror("fopen router_file");
        exit(-1);
    }
    char buf[MAX_LINE];
    fgets(buf, MAX_LINE, f);
    buf[strlen(buf)-1]='\0';
    location_num = atoi(buf);

    for(int i = 0; i < location_num; ++i)
    {
        fgets(buf, MAX_LINE, f);
        int len = strlen(buf);
        buf[len-1] = '\0';
        int j = 0;
        for(;; ++j)
        {
            if(buf[j] == ',')
            {
                tuple[i].ip[j] = '\0';
                break;
            }
            tuple[i].ip[j] = buf[j];
        }
        char port_tmp[10], id_tmp[10];
        memset(port_tmp, 0, 10);
        memset(id_tmp, 0, 10);
        int s = ++j;
        int e;
        for(;; ++j)
            if(buf[j] == ',')
            {
                e = j;
                break;
            }
        memcpy(port_tmp, buf+s, e-s);
        port_tmp[e-s] = '\0';
        tuple[i].port = atoi(port_tmp);
        s = ++j;
        e = len;
        memcpy(id_tmp, buf+s, e-s);
        id_tmp[e-s] = '\0';
        tuple[i].id = atoi(id_tmp);

        RtoV[tuple[i].id] = i+1;
        VtoR[i+1] = tuple[i].id;
    }
    fclose(f);
    /*
        for(int i = 0; i < MAX_ID; ++i)
            if(RtoV[i] != 0)
                printf("%d:%d  ", i, RtoV[i]);
        printf("\n");
        for(int i = 0; i < MAX_ROUTER; ++i)
            if(VtoR[i] != 0)
                printf("%d:%d  ", i, VtoR[i]);
        printf("\n");

        for(int i = 0; i < location_num; ++i)
        {
            printf(tuple[i].ip);
            printf("\n%d\n", tuple[i].port);
            printf("%d\n", tuple[i].id);
        }
    */
    run();
    return 0;
}
