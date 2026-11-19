#include <netinet/in.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<ifaddrs.h>
#include<net/if.h>
#include"common.h"
#include"lease.h"

uint8_t get_dhcp_message_type(const uint8_t *options, size_t max_len) {
    size_t i = 0;
    while (i < max_len) {
        uint8_t code = options[i];

        if (code == 255) { // End Option
            break;
        }
        if (code == 0) {   // Pad Option (1 byte, no length)
            i++;
            continue;
        }

        // Ensure Length byte exists and Value stays inside buffer bounds
        if (i + 1 >= max_len) break;
        uint8_t len = options[i + 1];
        if (i + 2 + len > max_len) break;

        if (code == 53 && len == 1) {
            return options[i + 2]; // Returns 1 (Discover), 3 (Request), etc.
        }

        i += 2 + len; // Jump to next TLV block
    }
    return 0; // Invalid / Not found
}

int add_option_ip(uint8_t *options, int offset, uint8_t code, uint32_t ip_net_order) {
    options[offset++] = code;
    options[offset++] = 4;
    memcpy(&options[offset], &ip_net_order, 4);
    return offset + 4;
}


size_t build_dhcp_offer(uint8_t *buffer, const uint8_t *client_mac, const uint8_t *server_mac, uint32_t *server_ip, uint32_t xid) {

    uint32_t offered_client_ip = allocate_ip_for_client(client_mac);

    // fills the first 1,500 bytes of memory pointed to by buffer with zeros.
    memset(buffer, 0, 1500);

    // Pointers to each layer inside the single buffer
    dhcp_t *dhcp = (dhcp_t *)(buffer);

    // ---------------------------------------------------------
    // 1. DHCP PAYLOAD (300 Bytes)
    // ---------------------------------------------------------
    dhcp->opcode       = 2;              // 2 = BOOTREPLY
    dhcp->htype        = 1;              // 1 = Ethernet
    dhcp->hlen         = 6;              // MAC address length
    dhcp->hops         = 0;
    dhcp->xid          = htonl(xid);     // Random Transaction ID
    dhcp->secs         = 0;
    dhcp->flags        = htons(0x8000);  // Broadcast flag (tells server to broadcast reply)
    dhcp->ciaddr       = 0;              // Must be 0 in DHCPOFFER because the client does not own the IP yet.
    dhcp->yiaddr       = offered_client_ip;              // Offered IP
    dhcp->siaddr       = (ip4_t)server_ip;
    dhcp->giaddr       = 0;
    memcpy(dhcp->chaddr, client_mac, 6); // Server MAC inside DHCP payload
    dhcp->magic_cookie = htonl(DHCP_MAGIC_COOKIE);

    // DHCP TLV Options
    int opt = 0;
    // Option 53: DHCP Message Type = 2 (DHCPOFFER)
    dhcp->options[opt++] = 53;
    dhcp->options[opt++] = 1;
    dhcp->options[opt++] = 2;

    // Option 54: Server Identifer
    opt = add_option_ip(dhcp->options, opt, 54, offered_client_ip);
    // dhcp->options[opt++] = 54;
    // dhcp->options[opt++] = 4; // 4 byte
    // dhcp->options[opt++] = offered_client_ip;


    // Option 1: subnet mask
    opt = add_option_ip(dhcp->options, opt, 1, htonl(inet_addr("255.255.255.0")));
    // dhcp->options[opt++] = 1;
    // dhcp->options[opt++] = 4;  // 4 byte
    // dhcp->options[opt++] = htonl(inet_addr("255.255.255.0"));  // Subnet mask

    // Option 3: Router/default gateway
    opt = add_option_ip(dhcp->options, opt, 3, htonl(inet_addr("192.158.50.1")));
    // dhcp->options[opt++] = 3;
    // dhcp->options[opt++] = 4;  // 4 byte
    // dhcp->options[opt++] = htonl(inet_addr("192.168.50.1"));  // Router default gateway

    // Option 6: DNS name server
    opt = add_option_ip(dhcp->options, opt, 6, htonl(inet_addr("8.8.8.8")));
    // dhcp->options[opt++] = 6;
    // dhcp->options[opt++] = 4;  // 4 byte
    // dhcp->options[opt++] = htonl(inet_addr("8.8.8.8"));  // Router default gateway

    // Option 255: End Option
    dhcp->options[opt++] = 255;

    size_t dhcp_len = sizeof(struct dhcp) + opt;
    return dhcp_len;
}

int get_interface_info(int fd, const char *if_name, uint32_t *ip_net_addr, uint8_t mac[6]) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, if_name, IF_NAMESIZE-1);

    // 1. Get the interface's IPv4 address (e.g., 192.168.50.1)
    if(ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        perror("SIOCGIFADDR failed (did you run ip addr add on veth_server?)");
        close(fd);
        return -1;
    }

    struct sockaddr_in *ipaddr = (struct sockaddr_in *)&ifr.ifr_addr;
    *ip_net_addr = ipaddr->sin_addr.s_addr;

    // 2. Get the interface's MAC address
    if(ioctl(fd, SIOCGIFHWADDR, &ifr) < 0 ) {
        perror("SIOCGIFHWADDR failed");
        close(fd);
        return -1;
    }

    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    return 0;
}


int main() {
    // Initalize lease pool
    init_lease_pool();

    // 1. Create the server socket (IPv4, UDP)
    int server_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    // 2. Lock the socket exclusively to the virtual server interface
    const char *if_name = "veth_server";
    if(setsockopt(server_fd, SOL_SOCKET, SO_BINDTODEVICE, if_name, strlen(if_name) + 1) < 0) {
        perror("SO_BINDTODEVICE failed (did you run with sudo?)");
        close(server_fd);
        return 1;
    }

    // 3. Allow sending broadcast replies (255.255.255.255)
    int broadcast_enable = 1;
    if(setsockopt(server_fd, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        perror("SO_BROADCAST failed");
        close(server_fd);
        return 1;
    }

    // 4. Define the server address structure
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all network interfaces
    address.sin_port = htons(67); // DHCP server listens on port 67

    // 3. Bind the socket to the IP and port
    if(bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind failed"); // Prints the descriptive error based on errno
        return 1;
    }

    // 4. Listen for incoming connections, max 5 in queue
    listen(server_fd, 5);
    printf("Server listening on port 67...\n");

    // 5. Get the IP and mac address for the server
    uint32_t server_ip;
    uint8_t server_mac[6];
    int status = get_interface_info(server_fd, if_name, &server_ip, server_mac);
    if(status != 0) {
        perror("Could not get server IP and MAC\n");
        exit(1);
    }

    u_int8_t *buffer = malloc(1500 * sizeof(char));
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while(1) {
        memset(buffer, 0, 1500);

        // 1. Blocks here (0% CPU) until a new UDP packet arrives on port 67
        ssize_t bytes_received = recvfrom(server_fd, buffer, 1500, 0,
                                          (struct sockaddr *)&client_addr, &addr_len);
        if (bytes_received < 0) {
            perror("recvfrom failed");
            continue;
        }

        // 2. Verify it is at least a valid DHCP header size (240 bytes with magic cookie)
        if (bytes_received < 240) {
            continue;
        }
        PRINT(VERBOSE_LEVEL_INFO, "\n[+] New DHCP packet received (%zd bytes)!\n", bytes_received);

        dhcp_t *dhcp = (dhcp_t *) buffer;
        PRINT(VERBOSE_LEVEL_INFO, "OP code is %d and Htype is %d\n", dhcp->opcode, dhcp->htype);

        u_int32_t xid = dhcp->xid;

        u_int8_t *options = dhcp->options;
        u_int8_t client_mac[6];
        memcpy(&client_mac, dhcp->chaddr, 6);
        uint8_t message_type = get_dhcp_message_type(options, 255);
        PRINT(VERBOSE_LEVEL_INFO, "Message type is: %d\n", message_type);

        if(message_type == 1) {
            PRINT(VERBOSE_LEVEL_INFO, "received dhcp discover");

            // Build DHCP offer
            u_int8_t *buffer = malloc(1500 * sizeof(char));
            size_t packet_len = build_dhcp_offer(buffer, client_mac, server_mac, &server_ip, xid);
            dhcp_t *dhcp_packet = (dhcp_t *)buffer;
            PRINT(VERBOSE_LEVEL_INFO, "Total DHCP offer packet len is: %ld\n", packet_len);

            // Target 255.255.255.255:68 (DHCP Client Port)
            struct sockaddr_in broadcast_addr;
            memset(&broadcast_addr, 0, sizeof(broadcast_addr));
            broadcast_addr.sin_family      = AF_INET;
            broadcast_addr.sin_port        = htons(68);
            broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

            // Send the DHCPOFFER out of server_fd
            ssize_t bytes_sent = sendto(server_fd, buffer, packet_len, 0,
                                        (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
            if (bytes_sent < 0) {
                perror("sendto failed (did you enable SO_BROADCAST?)");
            } else {
                printf("Sent DHCPOFFER (%zd bytes) to 255.255.255.255:68\n", bytes_sent);
            }
        }
    }

    close(server_fd);
    return 0;

}
