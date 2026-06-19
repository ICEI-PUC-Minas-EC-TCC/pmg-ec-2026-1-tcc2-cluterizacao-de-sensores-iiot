#pragma once

#include "AmmeterService/ammeter_service.hpp"

// Persistência do consumo de bateria do ammeter no NVS, compartilhada pelos
// dois backends (ADC e INA219), que duplicam o mesmo modelo de coulomb
// counting. consumed_mah é a fonte da verdade; remaining_mah/battery_pct são
// derivados dele.
namespace service::ammeter::persistence {

// Carrega consumed_mah/consumed_mwh do NVS (se existirem) e deriva
// remaining_mah/battery_pct. Sem dados persistidos, deixa a bateria como está
// (tipicamente cheia, definida pelo init do backend).
void load(Measurement &m, float capacity_mah);

// Grava o consumo quando ele varia >= 1% da capacidade desde a última
// gravação (limita o desgaste do flash). Chamar a cada nova medição.
void maybe_persist(const Measurement &m, float capacity_mah);

} // namespace service::ammeter::persistence
