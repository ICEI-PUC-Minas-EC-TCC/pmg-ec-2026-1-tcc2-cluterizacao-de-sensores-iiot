#pragma once

#include <string>

namespace service::rtc {

// Configura o fuso horario e inicia o cliente SNTP (nao bloqueante).
// Deve ser chamado uma vez no boot, DEPOIS de controller::network::init()
// (que cria o esp_netif e o event loop default).
void init();

// Retorna a data e hora atuais como texto no formato brasileiro
// "dd/mm/aaaa hh:mm:ss" no fuso de Brasilia (UTC-3). Antes da primeira
// sincronizacao SNTP, formata o epoch cru (ex.: "01/01/1970 00:00:12").
std::string get_current_time();

// true apos a primeira sincronizacao SNTP bem-sucedida. Helper opcional;
// nao altera o comportamento de get_current_time().
bool is_synced();

} // namespace service::rtc
