#ifndef LEASE_H
#define LEASE_H

#include<stdio.h>
#include<stdlib.h>
#include<time.h>

#define POOL_SIZE 253 // 192.168.50.2 to 192.168.50.254

typedef enum {
    LEASE_FREE=0,
    LEASE_OFFERED, // Temporarily reserved during DHCPOFFER
    LEASE_BOUND    // Confirmed active after DHCPACK
} lease_state_t;

struct dhcp_lease {
    uint32_t ip_addr;       // Network byte order
    uint8_t mac;            // Client MAC (chaddr)
    lease_state_t state;
    time_t expires_at;      // Unix timestamp when lease expires
};

void init_lease_pool(void);

uint32_t allocate_ip_for_client(const uint8_t *mac);

bool bound_ip_for_client(uint32_t ip);

#endif
