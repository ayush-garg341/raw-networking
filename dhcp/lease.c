#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<arpa/inet.h>
#include<time.h>
#include<stdbool.h>
#include"lease.h"


struct dhcp_lease lease_pool[POOL_SIZE];

// Initialize pool on server startup
void init_lease_pool(void) {
    uint32_t base_ip = ntohl(inet_addr("192.168.50.2"));
    for(int i = 0; i < POOL_SIZE; i++) {
        lease_pool[i].ip_addr = htonl(base_ip + i);
        lease_pool[i].state = LEASE_FREE;
        lease_pool[i].expires_at = 0;
        memset(&lease_pool[i].mac, 0, 6);
    }
}

// Select an IP for an incoming DHCP DISCOVER
uint32_t allocate_ip_for_client(const uint8_t *mac) {
    time_t now = time(NULL);

    // 1. Check if this MAC already has a lease (active or expired)
    for(int i=0; i < POOL_SIZE; i++) {
        if(lease_pool[i].state != LEASE_FREE && memcmp(&lease_pool[i].mac, mac, 6) == 0) {
            lease_pool[i].state = LEASE_OFFERED;
            lease_pool[i].expires_at = now + 60;
            return lease_pool[i].ip_addr;
        }
    }

    // 2. Find first FREE or EXPIRED slot
    for(int i = 0; i < POOL_SIZE; i++) {
        if(lease_pool[i].state == LEASE_FREE || lease_pool[i].expires_at < now) {
            lease_pool[i].state = LEASE_OFFERED;
            lease_pool[i].expires_at = now + 60;
            return lease_pool[i].ip_addr;
        }
    }
    return 0; // Pool exhausted
}

bool bound_ip_for_client(uint32_t ip) {
    time_t now = time(NULL);
    for(int i = 0; i < POOL_SIZE; i++) {
        if(lease_pool[i].ip_addr == ip) {
            lease_pool[i].state = LEASE_BOUND;
            lease_pool[i].expires_at = now + 3600;
            return true;
        }
    }
    return false;
}
