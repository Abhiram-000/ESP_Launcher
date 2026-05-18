#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the Captive Portal DNS Server.
 * Intercepts all UDP port 53 traffic and resolves to 192.168.4.1.
 */
void dns_server_start(void);

/**
 * @brief Stop the Captive Portal DNS Server.
 */
void dns_server_stop(void);

#ifdef __cplusplus
}
#endif
