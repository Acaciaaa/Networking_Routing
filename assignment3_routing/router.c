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

int id = 0;
int location_num = 0;
int RtoV[MAX_ID];
int VtoR[MAX_ROUTER];

struct TUPLE tuple[MAX_ROUTER];
int cost[MAX_ROUTER];
int neighbors[MAX_ROUTER][MAX_ROUTER];
struct ROUTING_TABLE table[MAX_ROUTER];
int dv_count = 0;

int modify_table()
{
/*
    for(int i = 1; i < MAX_ROUTER; ++i)
    {
        printf("%d ", cost[i]);
    }
    printf("\n");
    for(int i = 1; i < MAX_ROUTER; ++i)
    {
        for(int j = 1; j < MAX_ROUTER; ++j)
            printf("%d:%d ", j, neighbors[i][j]);
        printf("\n");
    }
    for(int i = 1; i < MAX_ROUTER; ++i)
    {
        printf("dest:%d next:%d dis:%d ", i, table[i].next, table[i].dis);
    }
    printf("\n");
*/

    struct ROUTING_TABLE tmp[MAX_ROUTER];
    memset(tmp, 0, sizeof(tmp));

    for(int i = 1; i < MAX_ROUTER; ++i)
    {
        if(i == id)
        {
            tmp[i].next = i;
            tmp[i].dis = 0;
            continue;
        }
        int min_dis = 2000, min_next = 0;
        if(cost[i] > 0)
        {
            min_dis = cost[i];
            min_next = i;
        }
        for(int j = 1; j < MAX_ROUTER; ++j)
        {
            if(cost[j] > 0 && j != i && j != id && neighbors[j][i] > 0)
            {
                int k = cost[j] + neighbors[j][i];
                if(k < min_dis)
                {
                    min_dis = k;
                    min_next = j;
                }
            }
        }

        if(min_dis != 2000)
        {
            tmp[i].next = min_next;
            tmp[i].dis = min_dis;
        }
    }
/*
    for(int i = 1; i < MAX_ROUTER; ++i)
    {
        printf("dest:%d next:%d dis:%d ", i, tmp[i].next, tmp[i].dis);
    }
    printf("\n");
*/
    int flag = 0;
    for(int i = 1; i < MAX_ROUTER; ++i)
        if(tmp[i].dis != table[i].dis)
        {
            flag = 1;
            break;
        }
    memcpy((void*)table, (void*)tmp, sizeof(table));
    //printf("flag:%d\n", flag);
    return flag;
}

void bellman_ford(int sockfd, const struct sockaddr *to, socklen_t tolen, int nei_num, int lack_num)
{
    // used by debug
    int seq = 0; // which turn
    //printf("lack_num %d nei_num %d\n", lack_num, nei_num);//debug

    while(1)
    {
        seq++; // debug
        int dv[MAX_ROUTER];
        memset(dv, 0, sizeof(dv));
        for(int i = 1; i < MAX_ROUTER; ++i)
            dv[i] = table[i].dis;

        // propogate
        for(int i = 1; i < MAX_ROUTER; ++i)
        {
            if(cost[i] == 0)
                continue;
            for(int j = 0; j < location_num; ++j)
            {
                if(tuple[j].id == VtoR[i])
                {
                    struct sockaddr_in receiver_addr;
                    memset(&receiver_addr, 0, sizeof(receiver_addr));
                    receiver_addr.sin_family = AF_INET;
                    receiver_addr.sin_port = htons(tuple[j].port);
                    // convert IPv4 or IPv6 addresses from text to binary form
                    if(inet_pton(AF_INET, tuple[j].ip, &receiver_addr.sin_addr)<=0)
                    {
                        perror("address failed");
                        exit(EXIT_FAILURE);
                    }
                    my_sendto(sockfd, (void*)dv, sizeof(dv), ROUTER_S, id, (struct sockaddr*)&receiver_addr, sizeof(struct sockaddr));
                }
            }
        }
        //printf("propogate %d lacknum %d\n", seq, lack_num);//debug

        // receive neighbors
        struct sockaddr_in sender;
        socklen_t addr_len = sizeof(struct sockaddr);
        int len, source, type;
        for(int i = 0; i < lack_num; ++i)
        {
            memset(dv, 0, sizeof(dv));
            my_recvfrom(sockfd, (void*)dv, &len, (struct sockaddr*)&sender, &addr_len, &source, &type);
            if(source != ROUTER_S || len != sizeof(dv))
            {
                perror("router dv");
                exit(EXIT_FAILURE);
            }
            memcpy(neighbors[type], dv, sizeof(dv));
        }

        // reply to agent
        if(modify_table() == 1)
            my_sendto(sockfd, NULL, 0, ROUTER_S, 4, to, tolen);
        else
            my_sendto(sockfd, NULL, 0, ROUTER_S, 3, to, tolen);

        lack_num = nei_num;

        // receive from agent, might be router then preserve it
        while(1)
        {
            memset(dv, 0, sizeof(dv));
            my_recvfrom(sockfd, (void*)dv, &len, (struct sockaddr*)&sender, &addr_len, &source, &type);
            if(source == ROUTER_S)
            {
                if(len != sizeof(dv))
                {
                    perror("router dv");
                    exit(EXIT_FAILURE);
                }
                memcpy(neighbors[type], dv, sizeof(dv));
                lack_num--;
                //printf("wa a fast baby:%d lacknum %d\n", VtoR[type], lack_num);//debug
            }
            else  // AGENT_S
            {
                printf("turn %d end, agent say %d\n", seq, type);//debug
                if(type == 0)
                    break;
                if(type != 4)
                {
                    perror("impossible");
                    exit(EXIT_FAILURE);
                }
                my_sendto(sockfd, NULL, 0, ROUTER_S, 1, to, tolen);
                return;
            }
        }
    }
}

void reply_update(int sockfd, void* buf, const struct sockaddr *to, socklen_t tolen)
{
    int* a = (int*)buf;
    int r = *a, weight = *(a+1);
    if(weight <= 0 || weight > 1000)
    {
        if(cost[r] == 0)
        {
            my_sendto(sockfd, NULL, 0, ROUTER_S, 1, to, tolen);
            return;
        }
        cost[r] = 0;
        memset(neighbors[r], 0, sizeof(int) * MAX_ROUTER);
        // hard to change routing table...
        memset(table, 0, sizeof(table));
        for(int i = 1; i < MAX_ROUTER; ++i)
        {
            if(i == id)
                table[i].next = i;
            if(cost[i] > 0)
            {
                table[i].next = i;
                table[i].dis = cost[i];
            }
        }
        //modify_table();
        my_sendto(sockfd, NULL, 0, ROUTER_S, 1, to, tolen);
    }
    else
    {
        if(weight == cost[r])
        {
            my_sendto(sockfd, NULL, 0, ROUTER_S, 1, to, tolen);
            return;
        }
        cost[r] = weight;
        for(int i = 1; i < MAX_ROUTER; ++i)
            neighbors[r][i] = r == i ? 0 : -1;
        // hard to change routing table...
        memset(table, 0, sizeof(table));
        for(int i = 1; i < MAX_ROUTER; ++i)
        {
            if(i == id)
                table[i].next = i;
            if(cost[i] > 0)
            {
                table[i].next = i;
                table[i].dis = cost[i];
            }
        }
        //modify_table();
        my_sendto(sockfd, NULL, 0, ROUTER_S, 1, to, tolen);
    }
}

void run(int me)
{
    int sockfd = 0;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof(struct sockaddr_in));
    address.sin_port = htons(tuple[me].port);
    address.sin_family = AF_INET;
    // convert IPv4 or IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, tuple[me].ip, &address.sin_addr)<=0)
    {
        perror("address failed");
        exit(EXIT_FAILURE);
    }
    // bind rtp socket to address
    if(bind(sockfd, (struct sockaddr *)&address, sizeof(struct sockaddr))<0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in sender;
    socklen_t addr_len = sizeof(struct sockaddr);
    char buf[BUFFER_SIZE];
    while(1)
    {
        memset(buf, 0, BUFFER_SIZE);
        int len, source, type;
        int nei_num = 0;

        my_recvfrom(sockfd, buf, &len, (struct sockaddr*)&sender, &addr_len, &source, &type);
        if(source == AGENT_S)
        {
            switch(type)
            {
            case 0:// dv
                for(int i = 1; i < MAX_ROUTER; ++i)
                    if(cost[i] > 0)
                        ++nei_num;
                //printf("nei:%d start to bellman-ford\n", nei_num);
                bellman_ford(sockfd, (struct sockaddr*)&sender, sizeof(struct sockaddr), nei_num, nei_num);
                break;

            case 1:// update
                if(len != sizeof(int)*2)
                {
                    perror("update buf");
                    exit(EXIT_FAILURE);
                }
                reply_update(sockfd, buf, (struct sockaddr*)&sender, sizeof(struct sockaddr));
                break;

            case 2:// show
                my_sendto(sockfd, (void*)table, sizeof(table), ROUTER_S, 2, (struct sockaddr*)&sender, sizeof(struct sockaddr));
                break;

            case 3:// reset
                dv_count = 0;
                my_sendto(sockfd, NULL, 0, ROUTER_S, 1, (struct sockaddr*)&sender, sizeof(struct sockaddr));
                break;

            default:// end dv or something else
                perror("agent type");
                exit(EXIT_FAILURE);
            }
        }
        else // ROUTER_S
        {
            printf("big loop receives ROUTER_S\n");
            if(len != sizeof(cost))
            {
                perror("router dv");
                exit(EXIT_FAILURE);
            }
            memcpy(neighbors[type], buf, sizeof(cost));

            for(int i = 1; i < MAX_ROUTER; ++i)
                if(cost[i] > 0)
                    ++nei_num;
            int lack_num = nei_num - 1;


            while(1)
            {
                memset(buf, 0, sizeof(buf));
                my_recvfrom(sockfd, (void*)buf, &len, (struct sockaddr*)&sender, &addr_len, &source, &type);
                if(source == ROUTER_S)
                {
                    if(len != sizeof(cost))
                    {
                        perror("router dv");
                        exit(EXIT_FAILURE);
                    }
                    memcpy(neighbors[type], buf, sizeof(cost));
                    lack_num--;
                    //printf("wa a fast baby:%d lacknum %d\n", VtoR[type], lack_num);//debug
                }
                else  // AGENT_S
                {
                    if(type != 0)
                    {
                        perror("not dv order");
                        exit(EXIT_FAILURE);
                    }
                    bellman_ford(sockfd, (struct sockaddr*)&sender, sizeof(struct sockaddr), nei_num, lack_num);
                    break;
                }
            }
        }
    }
    close(sockfd);
}

int main(int argc, char **argv)
{
    char *router_file;
    char *topology_file;
    int myid;

    if (argc != 4)
    {
        fprintf(stderr, "Usage: ./router <router location file> <topology conf file> <router id>");
        exit(EXIT_FAILURE);
    }

    router_file = argv[1];
    topology_file = argv[2];
    myid = atoi(argv[3]);

    // init
    memset(RtoV, 0, sizeof(int)*MAX_ID);
    memset(VtoR, 0, sizeof(int)*MAX_ROUTER);
    for(int i = 0; i < MAX_ROUTER; ++i)
    {
        memset(tuple[i].ip, 0, IP_LEN);
        tuple[i].id = 0;
        tuple[i].port = 0;

        cost[i] = 0;

        memset(neighbors[i], 0, sizeof(int)*MAX_ROUTER);

        table[i].next = 0;
        table[i].dis = 0;
    }
    table[RtoV[myid]].next = RtoV[myid];
    table[RtoV[myid]].dis = 0;

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
    memset(buf, 0, MAX_LINE);

    // parse topology
    f = fopen(topology_file, "r");
    if(f == NULL)
    {
        perror("fopen topology_file");
        exit(-1);
    }
    fgets(buf, MAX_LINE, f);
    buf[strlen(buf)-1]='\0';
    int topology_num = atoi(buf);

    for(int i = 0; i < topology_num; ++i)
    {
        char ra[10], rb[10], d[10];
        memset(ra, 0, 10);
        memset(rb, 0, 10);
        memset(d, 0, 10);
        fgets(buf, MAX_LINE, f);
        int len = strlen(buf);
        buf[len-1] = '\0';
        int j = 0, s = 0, e;
        for(;; ++j)
            if(buf[j] == ',')
            {
                e = j;
                break;
            }
        memcpy(ra, buf, e-s);
        ra[e-s] = '\0';
        //printf("%d\n", atoi(ra));
        if(atoi(ra) != myid)
            continue;
        int nei, dd;
        s = ++j;
        for(;; ++j)
            if(buf[j] == ',')
            {
                e = j;
                break;
            }
        memcpy(rb, buf+s, e-s);
        rb[e-s] = '\0';
        nei = atoi(rb);
        //printf("%d\n", nei);
        s = ++j;
        e = len;
        memcpy(d, buf+s, e-s);
        d[e-s] = '\0';
        dd = atoi(d);
        //printf("%d\n", dd);
        if(dd <= 0 || dd > 1000)
            continue;

        // init cost, neighbors, table
        int r = RtoV[nei];
        cost[r] = dd;
        //printf("%d:%d ", nei, r);
        for(int k = 1; k < MAX_ROUTER; ++k)
            neighbors[r][k] = r == k ? 0 : -1;
        table[r].next = r;
        table[r].dis = dd;
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

        printf("next!!!\n");
        for(int i = 1; i < MAX_ROUTER; ++i)
            printf("%d:%d  ", i, cost[i]);
        printf("\n");
        for(int i = 1; i < MAX_ROUTER; ++i){
        printf("%d:  ", i);
            for(int j = 1; j < MAX_ROUTER; ++j)
                printf("%d  ", neighbors[i][j]);
        printf("\n");
        }
        for(int i = 1; i < MAX_ROUTER; ++i)
            printf("%d,%d  ", table[i].next, table[i].dis);
        printf("\n");
    */
    for(int i = 0; i < location_num; ++i)
        if(tuple[i].id == myid)
        {
            id = RtoV[myid];
            run(i);
            break;
        }
    return 0;
}
