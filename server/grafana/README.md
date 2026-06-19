# Dashboard Grafana — comparação de políticas (energy vs round_robin)

Compara as políticas de roteamento pela **bateria ao longo do tempo decorrido**, com as
duas curvas sobrepostas começando juntas em `00:00`. Pensado para a monografia.

Arquivo: [`comparacao_politicas.json`](comparacao_politicas.json) — pronto para importar.

## Painéis

1. **Bateria do nó mais fraco por política (tempo decorrido)** — uma linha por política,
   mostrando, a cada instante, a bateria do nó mais fraco da rede (o que determina o FND).
   Onde a linha toca `0` (ou o limiar), aquela política sofreu o *First Node Death*.
2. **FND por política** — número direto, em minutos de teste, até o primeiro nó cruzar o
   limiar de bateria. Maior = política mantém a rede viva por mais tempo.

## Como importar

1. Grafana → **Dashboards → New → Import**.
2. Faça upload do `comparacao_politicas.json`.
3. Quando pedir o datasource `InfluxDB`, selecione seu datasource **InfluxDB 2.x em modo
   Flux** apontando para a org `meu_tcc` / bucket `dados_esps`.

> Se ainda não houver um datasource Flux: Connections → Data sources → InfluxDB → Query
> language = **Flux**, URL `http://64.181.160.152:8086`, org `meu_tcc`, token do bucket.

## Por que o eixo X fica em "1970"

Os dois testes rodam em horários reais diferentes. Para sobrepor as curvas de forma honesta,
a query **normaliza o tempo**: `_time` vira *offset desde o início de cada teste* (`t0` = 1º
ponto da política), ancorado no epoch Unix. Por isso o eixo lê `00:00, 00:30, 01:00…` —
isso é **tempo decorrido de teste**, não data real. `00:30` = 30 min de ensaio.

Consequência prática: o **seletor de tempo** (canto superior direito) controla *quanto do
tempo decorrido aparece* e já vem fixado em `1970-01-01 00:00 → 08:00`. Se um teste durar
mais que 8 h, aumente o `to`. **Não** mexa nele para "ver dados de hoje" — para isso existem
as variáveis abaixo.

## Variáveis (topo do dashboard)

| Variável       | Default       | Para quê |
|----------------|---------------|----------|
| `range_start`  | `-30d`        | Início da janela **real** (relógio) que contém os ensaios. A busca no Influx usa isto. |
| `range_stop`   | `now()`       | Fim da janela real. |
| `bucket`       | `30s`         | Tamanho da janela de amostragem do nó mais fraco (`30s`, `1m`, `5m`). |
| `fnd_thresh`   | `5`           | Bateria (%) na qual um nó é considerado "morto" para o FND. |
| `bucket_influx`| `dados_esps`  | Nome do bucket no InfluxDB (oculto). |
| `run`          | *(query)*     | Rodada de teste (carimbada no reset do BOOT). Lista os valores da tag `run` no bucket. |

**Importante para isolar um único ensaio:** se você rodar vários testes da mesma política ao
longo de semanas, `-30d` pega todos e o `t0` vira o do primeiro. Para cercar um ensaio
específico, ajuste `range_start`/`range_stop` para a janela real daquele dia (ex.
`2026-06-20T14:00:00Z` … `2026-06-20T22:00:00Z`).

## De onde vêm os dados

O bridge [`../mqtt_to_influx.py`](../mqtt_to_influx.py) grava cada leitura em InfluxedB:

- measurement: `consumo`
- tags: `node`, `papel`, `policy`, `run`  ← a tag `policy` (`energy` / `round_robin`) é o que separa os ensaios; `run` identifica a rodada (carimbada no reset)
- fields: `current_ma`, `battery_pct`

> A tag `policy` só existe em dados gravados pela versão atual do bridge. Garanta que o
> firmware publica `policy` no JSON e que cada ensaio roda com a sua política.

## Rodando um ensaio

### Gestos de BOOT e controle de reset

O firmware reconhece três faixas de tempo no botão de BOOT (hold contínuo), cada uma triggerando um modo de reset diferente:

| Faixa     | Comportamento | Cenário |
|-----------|---------------|---------|
| 2–5 s    | **Restart suave** — reinicia apenas o ESP, sem resetar dados de energia no NVS. | Recuperação de travamento, testes rápidos. |
| 5–10 s   | **Reset CHEIO** (Cenário A) — limpa NVS, todos os nós voltam com bateria em **100%**. | Ensaio homogêneo; todos começam com a mesma energia. |
| >10 s    | **Reset ESCALONADO** (Cenário B) — limpa NVS, cada nó recebe bateria inicial conforme sua posição na lista **ordenada por MAC**: posição 1 → 100%, 2 → 85%, 3 → 70%, 4 → 55%, 5 → 40%. | Ensaio heterogêneo; simula rede já descarregada antes do teste. |

Ao soltar o botão, o reset inicia: o nó publica via broadcast seu novo `run_id` (um UUID único da rodada) e transmite para toda a malha o scenario escolhido (CHEIO ou ESCALONADO).

### Morte simulada e FND

Quando a bateria medida de um nó atinge **~1%** (limiar `DEATH_THRESHOLD_PCT`), o nó:
- **Para de publicar** MQTT (não envia mais leituras de corrente/bateria).
- **Recusa participar** da liderança de roteamento (não se oferece como roteador).
- **Step-down** automático até ficar com apenas link direto ao concentrador (se houver).

Isso é **comportamento esperado e proposital** — não é um bug, mas a simulação de uma morte real por bateria. O nó não desaparece da memória da malha imediatamente; quando a query Grafana filtra `battery >= fnd_thresh` (padrão 5%), o painel FND registra o instante em que o nó "morre" para o teste.

### Selecionando a rodada (run_id) no dashboard

A variável `run` (tabela acima) lista automaticamente todos os `run_id` únicos gravados nos últimos 30 dias (ou conforme `range_start`/`range_stop`). Cada reset (em qualquer dos três modos) gera um novo UUID que é:
1. Gerado no RTC do nó resgatador (em coordenação com NTP ou clock anterior).
2. Transmitido via broadcast para toda a malha.
3. Gravado no NVS de cada nó e publicado em cada leitura MQTT (tag `run`).
4. Ingerido pelo bridge [`../mqtt_to_influx.py`](../mqtt_to_influx.py) na tag `run` do InfluxDB.

Para **isolar um ensaio específico** no dashboard:
- Selecione o `run_id` no dropdown `run` (aparece como timestamp ou UUID da rodada).
- Se várias rodadas existem no mesmo horário, use `range_start`/`range_stop` para estreitar a janela real (ex. `2026-06-20T14:00:00Z` … `2026-06-20T14:30:00Z`).

### Cenário A (CHEIO) vs Cenário B (ESCALONADO) — quando usar

**Cenário A — Reset CHEIO (5–10 s)**
- Todos os nós começam com **100% de bateria**.
- **Homogêneo**: mesmas condições iniciais para ambas as políticas.
- **Uso na monografia**: comparação "fair" entre política 1 e política 2; isola o efeito do algoritmo sem confundidor de estado inicial.
- Exemplo: "Política A mantém o FND por X minutos, política B por Y minutos, partindo de igualdade."

**Cenário B — Reset ESCALONADO (>10 s)**
- Nós têm bateria inicial: **[100%, 85%, 70%, 55%, 40%]** (por ordem de MAC).
- **Heterogêneo**: simula uma malha que já sofreu desgaste desigual (nós periféricos descarregam mais cedo que centrais em topologias reais).
- **Uso na monografia**: avalia resiliência sob condições de bateria já degradada; mostra se a política consegue "rebalancear" o consumo quando algum nó já chega fraco.
- Exemplo: "Política A degrada-se quando nós periféricos chegam com <60% de bateria, política B mantém a distribuição mesmo com essa heterogeneidade."

**Escolha recomendada:**
- Se o foco é **algoritmo vs algoritmo**, use Cenário A (ambas políticas mesma base).
- Se o foco é **robustez em malhas reais** (que já têm desgaste acumulado), use Cenário B.
- Para cobertura completa na monografia, rode **ambas** e compare os gráficos de FND lado a lado.

## Lógica da query (resumo)

`from() |> range(janela real)` → acha `t0` por política (`group by policy |> sort |> first`)
→ `join.left` injeta `t0` em cada leitura → `map` reescreve `_time = _time - t0` (epoch) →
nó mais fraco por bucket (`truncateTimeColumn |> group(policy,_time) |> min`). O FND filtra
`battery <= fnd_thresh`, pega o `first` por política e converte o offset para minutos.

Queries validadas contra InfluxDB **v2.8.0** (requer o pacote Flux `join`, presente nessa versão).
