/**
 * @file dns_server.c
 * @brief Lightweight DNS Server on port 53 for captive portal redirection.
 */

#include "dns_server.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "DNS_SRV";
static TaskHandle_t s_dns_task_handle = NULL;
static volatile bool s_run_dns = false;
static int s_dns_socket = -1;

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

static void dns_server_task(void *pvParameters) {
    ESP_LOGI(TAG, "DNS Server Task Started");
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(53);

    s_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_dns_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    // Set non-blocking or timeout to allow clean shutdowns
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(s_dns_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (bind(s_dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: errno %d", errno);
        close(s_dns_socket);
        s_dns_socket = -1;
        vTaskDelete(NULL);
        return;
    }

    uint8_t *rx_buf = malloc(512);
    if (!rx_buf) {
        ESP_LOGE(TAG, "OOM allocating Rx buffer");
        close(s_dns_socket);
        s_dns_socket = -1;
        vTaskDelete(NULL);
        return;
    }

    while (s_run_dns) {
        struct sockaddr_storage source_addr;
        socklen_t socklen = sizeof(source_addr);
        
        int len = recvfrom(s_dns_socket, rx_buf, 512, 0, (struct sockaddr *)&source_addr, &socklen);
        if (len < (int)sizeof(dns_header_t)) {
            continue; // Timeout or small invalid packet
        }

        dns_header_t *header = (dns_header_t *)rx_buf;
        
        // Only respond to standard queries (A-record / wildcard)
        uint16_t qdcount = ntohs(header->qdcount);
        if (qdcount > 0) {
            // Repurpose headers as response
            header->flags = htons(0x8400); // Standard response, authoritative
            header->ancount = htons(1);    // 1 Answer RR
            header->nscount = 0;
            header->arcount = 0;

            // Find the end of the query section (Question QNAME)
            int qname_idx = sizeof(dns_header_t);
            while (qname_idx < len && rx_buf[qname_idx] != 0) {
                qname_idx += rx_buf[qname_idx] + 1;
            }
            qname_idx++; // Skip null byte

            // Skip QTYPE (2 bytes) & QCLASS (2 bytes)
            int q_end = qname_idx + 4;
            if (q_end <= len) {
                // Construct the answer record at the end of the query
                uint8_t *answer = rx_buf + q_end;
                
                // Name points to the QNAME offset (offset 12)
                answer[0] = 0xC0;
                answer[1] = 0x0C;
                
                // Type A (0x0001)
                answer[2] = 0x00;
                answer[3] = 0x01;
                
                // Class IN (0x0001)
                answer[4] = 0x00;
                answer[5] = 0x01;
                
                // TTL 60 seconds (0x0000003C)
                answer[6] = 0x00;
                answer[7] = 0x00;
                answer[8] = 0x00;
                answer[9] = 0x3C;
                
                // Data length 4 bytes (0x0004)
                answer[10] = 0x00;
                answer[11] = 0x04;
                
                // Resolved IP: 192.168.4.1
                answer[12] = 192;
                answer[13] = 168;
                answer[14] = 4;
                answer[15] = 1;

                int resp_len = q_end + 16;
                sendto(s_dns_socket, rx_buf, resp_len, 0, (struct sockaddr *)&source_addr, socklen);
            }
        }
    }

    free(rx_buf);
    close(s_dns_socket);
    s_dns_socket = -1;
    s_dns_task_handle = NULL;
    ESP_LOGI(TAG, "DNS Server Task Stopped");
    vTaskDelete(NULL);
}

void dns_server_start(void) {
    if (s_dns_task_handle != NULL || s_run_dns) {
        ESP_LOGI(TAG, "DNS Server already running");
        return;
    }
    s_run_dns = true;
    xTaskCreatePinnedToCore(dns_server_task, "dns_task", 4096, NULL, 3, &s_dns_task_handle, 0);
}

void dns_server_stop(void) {
    if (!s_run_dns) return;
    ESP_LOGI(TAG, "Stopping DNS Server...");
    s_run_dns = false;
    // The task will exit its loop on timeout and clean up itself
}
