#pragma once

#include "esp_wifi.h"

namespace driver::wifi {

// Sobe o stack em APSTA: um SoftAP placeholder no canal fixo mantem o radio
// acordado (ESP-NOW) e o STA fica DESASSOCIADO ate connect() ser chamado.
void init();

// Associa o STA ao AP configurado (chamado ao virar LEADER). Idempotente.
void connect();

// Desassocia o STA para economizar bateria (chamado ao virar MEMBER). O
// SoftAP e o ESP-NOW continuam ativos. Idempotente; nao dispara reconexao
// automatica.
void disconnect();

}
