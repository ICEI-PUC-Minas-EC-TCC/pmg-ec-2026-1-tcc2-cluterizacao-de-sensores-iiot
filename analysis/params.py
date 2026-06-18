"""Constantes espelhando o firmware. Fonte entre colchetes [arquivo:linha]."""

LOOP_PERIOD_MS = 100            # [main/src/Application/application_controller.cpp:70]

TERM_DURATION_MS = 60_000       # [main/src/Application/role_service.cpp:31]
DISCOVERY_WINDOW_MS = 2_000     # [main/src/Application/role_service.cpp:26]
ISOLATION_TIMEOUT_MS = 60_000   # [main/src/Application/role_service.cpp:36]
ROTATE_RETRIES = 10             # [main/src/Application/role_service.cpp:40]
ROTATE_RETRY_MS = 200           # [main/src/Application/role_service.cpp:41]

COOLDOWN_MS = 120_000           # [main/src/Application/leader_policy.cpp:31]

PEER_TTL_MS = 10_000            # [main/src/Application/energy_service.cpp:28]
ENERGY_TICK_PERIOD_MS = 1_000   # [main/src/Application/energy_service.cpp:24]
INITIAL_BUDGET = 100_000        # [main/src/Application/energy_service.cpp:15]
COST_MQTT = 50                  # [main/src/Application/energy_service.cpp:19]
COST_ESPNOW_SEND = 5            # [main/src/Application/energy_service.cpp:20]
COST_TICK = 1                   # [main/src/Application/energy_service.cpp:21]

PING_PERIOD_MS = 1_000          # [main/src/Application/discover_service.cpp:13]
READING_INTERVAL_MS = 2_000     # [main/src/Application/reading_service.cpp:9]

BATTERY_CAPACITY_MAH = 1_000    # [main/Kconfig.projbuild AMMETER_BATTERY_CAPACITY_MAH]
BATTERY_VOLTAGE_MV = 3_700      # [main/Kconfig.projbuild AMMETER_BATTERY_VOLTAGE_MV]

# Transporte (sim). PING broadcast ~20% recepção [role_service.cpp:39 comment];
# READING/ROTATE unicast confiável [network_service.cpp:198-203].
PING_BROADCAST_LOSS = 0.80
UNICAST_LOSS = 0.0
