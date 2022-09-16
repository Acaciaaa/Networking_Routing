#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>

#define MAX_WEIGHT 1000
#define MAX_ROUTER 12
#define MAX_ID 100
#define IP_LEN 18

#define AGENT_S 0
#define ROUTER_S 1

struct TUPLE{
    char ip[IP_LEN];
    int port;
    int id;
};

struct ROUTING_TABLE{
    int next;
    int dis;
};

typedef struct __attribute__ ((__packed__)) MY_header {
    uint8_t source;       // 0: agent; 1: router
    // 0: dv; 1: update; 2: show; 3: reset; 4: end dv;
    // (to router)id;
    // (to agent)1: ok; 2: reply routing table; 3: stable; 4: not stable;
    uint16_t type;
    uint32_t length;
} my_header;
