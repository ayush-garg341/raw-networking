#include <netinet/in.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<arpa/inet.h>
#include"common.h"

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

// Usage when building DHCPOFFER:
// int opt = 0;
// dhcp->options[opt++] = 53; dhcp->options[opt++] = 1; dhcp->options[opt++] = 2; // Offer
// opt = add_option_ip(dhcp->options, opt, 1,  inet_addr("255.255.255.0"));
// opt = add_option_ip(dhcp->options, opt, 3,  inet_addr("192.168.50.1"));
// opt = add_option_ip(dhcp->options, opt, 6,  inet_addr("8.8.8.8"));
// opt = add_option_ip(dhcp->options, opt, 54, inet_addr("192.168.50.1"));
// dhcp->options[opt++] = 255; // End Option


int main() {
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

        u_int8_t *options = dhcp->options;
        uint8_t message_type = get_dhcp_message_type(options, 255);
        PRINT(VERBOSE_LEVEL_INFO, "Message type is: %d\n", message_type);

        if(message_type == 1) {
            PRINT(VERBOSE_LEVEL_INFO, "received dhcp discover");
            //TODO: Build dhcp offer
        }

    }

    close(server_fd);
    return 0;

}
