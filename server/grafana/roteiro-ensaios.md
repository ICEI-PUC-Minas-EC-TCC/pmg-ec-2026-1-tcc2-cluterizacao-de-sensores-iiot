# Checklist de ensaios — comparação das 3 políticas de eleição

Matriz: **3 políticas × 2 cenários = 6 ensaios**, com **5 ESPs**.
Política é escolha de *compile-time* → trocar de política = rebuild + reflash em todos os nós.
Cenário (energia inicial) é gesto de BOOT em tempo de execução.

| Política | Cenário A (100%) | Cenário B (escalonado) |
|----------|------------------|------------------------|
| `round_robin`    | ☐ | ☐ |
| `energy`         | ☐ | ☐ |
| `energy_cooldown`| ☐ | ☐ |

---

## ⚠️ Pré-condição crítica — canal Wi-Fi

O split-brain veio do canal. O firmware assume o AP no `CONFIG_NETWORK_FIXED_CHANNEL` (default **6**);
em APSTA (rádio único) o STA arrasta o SoftAP para o canal do AP ao associar.

- [ ] Hotspot do celular travado no **canal 6** — OU `CONFIG_NETWORK_FIXED_CHANNEL` ajustado para o canal real do hotspot (e rebuild).
- [ ] Se o canal do hotspot mudar no meio de um ensaio → dados contaminados, refazer.

## Pré-flight (uma vez, antes de tudo)

- [ ] Bridge `server/mqtt_to_influx.py` rodando e gravando em InfluxDB (`measurement=consumo`; tags `node/papel/policy/run`).
- [ ] Dashboard importado no Grafana — http://64.181.160.152:3000/d/tcc-politicas-energia
- [ ] Hotspot com internet (NTP no boot; sem isso o `run_id` vem errado).
- [ ] Fix de split-brain ativo: em cada run, **só um nó** publicando `"role":"LEADER"` por vez.

---

## Loop por política (repetir 3×, uma por política)

### Passo 1 — selecionar e gravar a política
- [ ] `idf.py menuconfig` → **Leader Election Policy** → escolher a política
- [ ] `idf.py build`
- [ ] `./flash_all.sh` (grava em **todos** os 5 nós)
- [ ] Confirmar no serial de cada nó: `LEADER_POLICY: Strategy: <round_robin|energy|energy_cooldown>` (todos iguais)

### Passo 2 — Ensaio A (100% / CHEIO)
- [ ] Num nó só: segurar **BOOT 5–10 s** e soltar → reset CHEIO, todos a 100%, novo `run_id`
- [ ] Anotar o `run_id` (log: `reset CHEIO em rede (run=...)`)
- [ ] Deixar rodar **até o FND** (1º nó cruzar ~1% e parar de publicar)
- [ ] Conferir no Grafana que os dados estão chegando (curva caindo)

### Passo 3 — Ensaio B (energias diferentes / ESCALONADO)
- [ ] Num nó só: segurar **BOOT >10 s** e soltar → reset ESCALONADO `[100,85,70,55,40]%` por ordem de MAC, novo `run_id`
- [ ] Anotar o `run_id`
- [ ] Deixar rodar **até o FND**

→ Voltar ao Passo 1 com a próxima política.

---

## Planilha de controle (preencher à medida)

| # | Política | Cenário | BOOT hold | run_id | Início | FND (min) |
|---|----------|---------|-----------|--------|--------|-----------|
| 1 | round_robin     | A (100%)        | 5–10 s | | 10:31 | 10:57 |
| 2 | round_robin     | B (escalonado)  | >10 s  | | 10:59 | 11:11 |
| 3 | energy          | A (100%)        | 5–10 s | | 11:30 | 12:02 |
| 4 | energy          | B (escalonado)  | >10 s  | | 12:04 | 12:25 |
| 5 | energy_cooldown | A (100%)        | 5–10 s | | 12:32 | 12:58 |
| 6 | energy_cooldown | B (escalonado)  | >10 s  | | 13:00 | 13:15|

---

## Lendo no Grafana

- [ ] Dropdown **`run`** (multi-seleção): marcar os 3 `run_id` do **mesmo cenário** (um por política)
- [ ] Conferir as 3 curvas sobrepostas, rotuladas `round_robin` / `energy` / `energy_cooldown`
- [ ] Painel **FND** mostra os 3 valores lado a lado
- [ ] Se a lista de `run` ficar grande, estreitar `range_start`/`range_stop` para a janela real do dia

## Critério de validade de cada run

- [ ] Um único líder por vez (sem dois `"role":"LEADER"` simultâneos no MQTT)
- [ ] Todos os 5 nós publicando (membros via líder)
- [ ] `run_id` é um timestamp real (NTP ok), não `UNKNOWN`
- [ ] Política correta na tag `policy` dos dados
