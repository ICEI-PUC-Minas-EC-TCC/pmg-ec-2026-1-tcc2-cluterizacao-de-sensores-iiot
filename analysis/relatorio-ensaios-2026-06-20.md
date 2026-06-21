# Relatório de ensaios — 2026-06-20

Comparação das 3 políticas de eleição de líder (`round_robin`, `energy`,
`energy_cooldown`) em 2 cenários de energia inicial (A = CHEIO 100%,
B = ESCALONADO). Rede de 5 ESP32-C3 (uplink MQTT por rodízio de liderança).

> **Resumo executivo.** O fix de split-brain funcionou (sem dois líderes
> persistentes). Porém **liderar custa praticamente a mesma corrente que ser
> membro** (~108 mA nos dois papéis), então o rodízio quase não redistribui
> energia neste hardware. A medição de **consumo total** (somando as correntes
> de todos os nós) fecha isso de forma **conclusiva**: a energia gasta por nó é
> idêntica nas 3 políticas e nos 2 cenários (~108–112 mA/nó, dentro de ~3 %) —
> ou seja, **a política de eleição não altera o consumo da rede**, e esse
> resultado não depende do modelo de bateria nem de quais nós sobreviveram.
> O *ranking* de FND entre políticas, esse sim, continua **inconclusivo**:
> vários ensaios estão comprometidos (resets perdidos, nó que caiu, `run_id`
> sem NTP, ensaio cortado, nº de nós diferente).

## Fontes e método

- **InfluxDB** (`meu_tcc`/`dados_esps`) — fonte preferida: tem as tags
  `policy`/`run`, separando cada ensaio sem depender de horário. Script:
  [`analyze_influx.py`](analyze_influx.py).
- **SQLite** (`sensores_iiot.db`) — fonte secundária (sem `policy`/`run`);
  segmentei por janelas derivadas dos resets. Script:
  [`../analyze_ensaios.py`](../analyze_ensaios.py).
- `timestamp` do SQLite é UTC (local = UTC−3). Descartei os primeiros 60 s de
  cada janela (transiente de reset/descoberta) e tópicos legados/lixo.
- FND = minutos do início do ensaio até o 1º nó cruzar **≤ 5 %** de bateria
  (mesmo limiar do dashboard).

## Mapa dos 6 ensaios (InfluxDB)

| Ensaio | policy | run_id | nós | duração |
|---|---|---|---|---|
| RR-A | round_robin | `31/12/1969 21:03:58` ⚠️ | 5 | 28 min |
| RR-B | round_robin | `20/06/2026 10:59:21` | 4 | 26 min |
| EN-A | energy | `31/12/1969 21:00:17` ⚠️ | 5 | 28 min |
| EN-B | energy | `20/06/2026 12:04:05` | 5 | 24 min |
| EC-A | energy_cooldown | `31/12/1969 21:00:52` ⚠️ | 4 | 28 min |
| EC-B | energy_cooldown | `20/06/2026 13:00:03` | 4 | 15 min |

⚠️ = `run_id` quebrado (`31/12/1969`): o RTC/NTP não estava sincronizado no
momento do reset, então o carimbo virou epoch. No dashboard esses ensaios
aparecem sob um `run` sem sentido.

## Resultados (InfluxDB — fonte autoritativa)

| Ensaio | Nós | FND da rede | I LEADER | I MEMBER |
|---|---|---|---|---|
| round_robin / A | 5 | **25,3 min** | 109,7 mA | 107,5 mA |
| round_robin / B | 4 | 9,5 min¹ | 108,4 mA | 106,9 mA |
| energy / A | 5 | **25,2 min** | 103,5 mA | 106,8 mA |
| energy / B | 5 | 21,7 min¹ | 110,0 mA | 107,2 mA |
| energy_cooldown / A | 4 | **25,1 min** | 109,8 mA | 108,8 mA |
| energy_cooldown / B | 4 | 13,5 min¹ | 110,7 mA | 108,4 mA |

¹ No cenário B o FND é determinado pelo nó que **começou mais fraco** (energia
inicial escalonada), não pela política — então B não compara políticas de forma
justa (ver problemas de qualidade).

**Cenário A (CHEIO, comparação justa):** as três políticas dão FND
**praticamente idêntico — ~25 min** (25,3 / 25,2 / 25,1). Como o estado inicial
é o mesmo, a política de rodízio **não alterou o tempo de vida da rede** — o que
é a consequência direta do achado #2 abaixo.

> SQLite (janelas derivadas dos resets) foi usado como verificação cruzada e
> bateu na ordem de grandeza; divergências (ex.: RR-A 28,5 min no SQLite) vêm
> de janelas/transientes diferentes. Os números acima, com `run`/`policy`, são
> os de referência.

## Consumo total da rede (análise definitiva)

A comparação por FND/bateria é frágil: depende de qual nó começou mais fraco, de
quantos nós sobreviveram e de o ensaio chegar ou não ao FND. O **consumo total** é
uma medição *end-to-end* de energia que contorna isso: somando a corrente de todos
os nós a cada instante temos a potência instantânea da rede; integrando no tempo
temos a carga gasta (mAh) — um único número que já inclui rádio, trocas de líder e
transientes, sem depender de modelo de bateria nem de qual nó morreu primeiro.
Script: [`analyze_consumo.py`](analyze_consumo.py) (resampling em buckets de 10 s,
`forward-fill` dentro da vida de cada nó, descarta os primeiros 60 s).

| Ensaio | Nós | I total (mA) | Q total (mAh) | **I por nó (mA)** | Q por nó (mAh) |
|---|---|---|---|---|---|
| RR-A | 5 | 528,7 | 237,9 | **109,2** | 47,6 |
| RR-B | 4 | 303,2 | 127,2 | **108,5** | 31,8 |
| EN-A | 5 | 515,5 | 233,4 | **107,4** | 46,7 |
| EN-B | 5 | 520,8 | 198,2 | **109,3** | 39,6 |
| EC-A | 4 | 432,5 | 192,2 | **110,7** | 48,1 |
| EC-B | 4 | 443,6 | 106,0 | **111,5** | 26,5 |

Os **totais brutos** (I total, Q total) variam, mas só por causa de confundidores:
nº de nós (4 vs 5) e duração do ensaio — **não** por causa da política. Normalizando
**por nó**, o efeito desaparece:

- **Cenário A (CHEIO):** I por nó = 109,2 / 107,4 / 110,7 mA → amplitude **3,3 mA
  (3,0 % da média)**.
- **Cenário B (ESCALONADO):** I por nó = 108,5 / 109,3 / 111,5 mA → amplitude **3,1 mA
  (2,8 % da média)**.

**Conclusão (definitiva): a política de eleição não altera o consumo total da rede.**
A energia gasta por nó é praticamente idêntica nas 3 políticas e nos 2 cenários
(~108–112 mA/nó, dentro de ~3 % — o mesmo nível de ruído entre nós). Diferente da
análise de FND, este resultado **não** depende do modelo de bateria nem de quais nós
sobreviveram, então é a confirmação robusta do achado #2: como liderar custa o mesmo
que ser membro, redistribuir a liderança não muda quanto a rede consome no agregado.

A figura `consumo_total_cenario_A.png` mostra a corrente total **plana ao longo de
todo o ensaio** nas 3 políticas (os patamares ~545 mA para RR/EN com 5 nós e ~445 mA
para EC com 4 nós refletem só o nº de nós); os degraus finais são nós morrendo perto
do FND.

### Corrente acumulada ao longo do tempo

Somando a corrente desde o início (começa em 0 e cresce: +~108 mA por amostra), temos
a **corrente acumulada** (mA) — o consumo somado no tempo, na unidade das leituras.
A curva de cada nó sobe de forma ~linear (consumo constante) e **achata quando o nó
morre**, no patamar final. Em `consumo_acumulado_<policy>_<cen>.png` (foco nos nós) as
curvas de um ensaio ficam praticamente sobrepostas (~17 000 mA/nó no cenário A — soma
em buckets de 10 s), confirmando que o rodízio gasta o mesmo em todos.

> O valor absoluto da soma depende do passo de amostragem (grade de 10 s), então é uma
> quantidade **visual/relativa**, boa para comparar nós e o formato da curva. A energia
> física comparável entre ensaios é a carga em **mAh** da tabela acima (= mA × horas;
> ~48 mAh/nó no cenário A).

Comparando a rede entre políticas (`consumo_acumulado_rede_cenario_A.png`): as 3
curvas são **retas de mesma natureza**; a diferença de inclinação é **só o nº de nós**
(5 em RR/EN vs 4 em EC), não a política — por nó a taxa é idêntica. Ou seja, o consumo
acumulado é constante e independente da política do início ao fim do ensaio.

## Achados

### 1. Fix de split-brain funcionou ✅
Em ~17 mil amostras, **nenhuma** sobreposição persistente de líderes. A única
ocorrência (energy_cooldown A, 1 segundo, `3fdc`+`42a4` às 15:47:30) é a janela
de step-down esperada (~2 s) — não um split-brain.

### 2. Liderar ≈ ser membro em consumo (achado central) 🔑
| Papel | Corrente média |
|---|---|
| LEADER | 108,0 mA |
| MEMBER | 107,3 mA |

Dentro do mesmo nó, a diferença LEADER−MEMBER fica entre **−6 e +5 mA** sobre
uma base de ~108 mA (ruído de ~5 %, sem sinal consistente):

| nó | I LEADER | I MEMBER | Δ |
|---|---|---|---|
| 3fdc | 105,7 | 111,7 | −6,0 |
| 42a4 | 109,9 | 107,7 | +2,2 |
| 89ec | 110,7 | 105,7 | +5,0 |
| 1d70 | 111,2 | 108,2 | +3,1 |
| 9f48 | 96,6 | 102,0 | −5,4 |

**Causa provável:** o rádio sempre ligado (APSTA + SoftAP para o ESP-NOW,
`WIFI_PS_NONE`) domina o consumo nos dois papéis; o STA+MQTT extra do líder é
marginal. **Consequência:** o rodízio de liderança quase não economiza energia
neste setup → no cenário A (mesma base) as 3 políticas **empatam em ~25 min de
FND**. Este é provavelmente o resultado mais relevante para a monografia.

### 3. Rodízio distribuiu bem a liderança
Nos cenários A, a liderança ficou a 6–10 pp do ideal (1/N por nó), confirmando
que eleição e rotação operam nas 3 políticas.

## Problemas de qualidade que limitam a comparação

A comparação política-a-política **não é conclusiva** porque vários ensaios
estão comprometidos:

1. **round_robin B**: o nó `3fdc` perdeu o reset escalonado (broadcast não
   chegou) → rodou com 2–4 nós. Inutilizável para comparar.
2. **9f48 caiu** após o energy B (sem resets depois) → energy_cooldown A/B com
   4/3 nós.
3. **`run_id` quebrado** (sem NTP) em RR-A, EN-A, EC-A → difícil isolar no
   dashboard.
4. **energy_cooldown B cortado** (coleta terminou 16:15, ninguém morreu) → sem
   FND.
5. **Escalonado saiu errado** em alguns (ex.: energy B com `89ec`/`1d70` em
   ~85 % em vez de 70/55 %) — `own_rank()` calculado antes da descoberta
   completa de peers.
6. **Nº de nós diferente entre ensaios** (5/5/4 nos A) → FND não é comparável
   maçã-com-maçã.

## Recomendações para uma rodada válida

1. **Garantir NTP antes do reset** (esperar sincronizar o RTC) para o `run_id`
   sair correto.
2. **Confirmar que o reset chega aos 5 nós** (checar log de cada um; aumentar
   repetições do broadcast ou resetar nó a nó).
3. No escalonado, **esperar a descoberta completa de peers** antes do reset
   (senão o rank/nível sai errado).
4. **Deixar os ensaios B chegarem ao FND** e manter os 5 nós vivos.
5. Dado o achado #2: para o rodízio mostrar economia, **diferenciar o custo do
   papel** — ex.: derrubar o SoftAP / usar power-save quando MEMBER. Sem isso a
   vantagem energética da rotação não aparece na medição.

## Figuras

Em [`figures/ensaios_2026-06-20/`](figures/ensaios_2026-06-20/):

- `bateria_<policy>_<cenario>.png` — bateria por nó ao longo do tempo.
- `comparacao_no_fraco_cenario_A.png` / `_B.png` — bateria do nó mais fraco,
  3 políticas sobrepostas.
- `fnd_por_ensaio.png` — FND por ensaio.
- `consumo_total_cenario_A.png` / `_B.png` — corrente total da rede (soma dos
  nós) ao longo do tempo, 3 políticas sobrepostas.
- `consumo_por_no.png` — corrente média e carga gasta por nó, por ensaio.
- `consumo_acumulado_<policy>_<cenario>.png` — corrente acumulada (mA) por nó ao
  longo do tempo (foco nos nós; um gráfico por ensaio).
- `consumo_acumulado_rede_cenario_A.png` / `_B.png` — corrente acumulada da rede
  (mA), 3 políticas sobrepostas.
