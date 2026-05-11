#ifndef MQTT_CONTROLLER_H
#define MQTT_CONTROLLER_H

#include <cstddef>
#include <stdint.h>

namespace controller::mqtt {

constexpr std::size_t TOPIC_BUFFER_SIZE = 64;
constexpr std::size_t PAYLOAD_BUFFER_SIZE = 256;

typedef struct {
    char topic[TOPIC_BUFFER_SIZE];
    char payload[PAYLOAD_BUFFER_SIZE];
} mqtt_msg_t;

void init();
void handler(void *arg);

void publish(const char *topic, const char *payload);

void set_wifi_status(bool connected);

}

#endif