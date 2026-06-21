# Dashboard Grafana — comparação de políticas (round_robin / energy / energy_cooldown)

Compara as políticas de roteamento pela **bateria ao longo do tempo decorrido**, com as
curvas sobrepostas começando juntas em `00:00`. Pensado para a monografia.

Suporta as **três** políticas. Como a política é escolha de *compile-time*, cada ensaio
(reflash) vira um `run` distinto. A variável `run` é **multi-seleção**: escolha um `run`
por política — todos do **mesmo cenário** (A ou B) — para sobrepor as curvas. Cada curva é
rotulada pela `policy` daquele `run`.

Arquivo: [`comparacao_politicas.json`](comparacao_politicas.json) — pronto para importar.

## Painéis

1. **Bateria do nó mais fraco por política (tempo decorrido)** — uma linha por `run`
   selecionado (rotulada pela política), mostrando, a cada instante, a bateria do nó mais
   fraco da rede (o que determina o FND). Onde a linha toca `0` (ou o limiar), aquele ensaio
   sofreu o *First Node Death*.
2. **FND por política** — número direto, em minutos de teste, até o primeiro nó cruzar o
   limiar de bateria, um por `run` selecionado. Maior = política mantém a rede viva por mais
   tempo.

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
| `run`          | *(query, multi)* | Rodada(s) de teste (carimbadas no reset do BOOT). **Multi-seleção**: escolha um `run` por política (mesmo cenário) para sobrepor as curvas. `All` (= `.*`) traz todos os runs da janela real. |

**Importante para isolar os ensaios certos:** o `t0` é calculado **por `run`** (cada ensaio
alinha em `00:00` pelo seu próprio início), então sobrepor políticas é só selecionar os
`run` corretos na variável `run`. Para comparar as 3 políticas de forma justa, selecione os
**3 `run` do mesmo cenário** (ex.: os três Cenário A). Se a lista de `run` ficar grande,
estreite `range_start`/`range_stop` para a janela real daquele dia (ex.
`2026-06-20T14:00:00Z` … `2026-06-20T22:00:00Z`).

## De onde vêm os dados

O bridge [`../mqtt_to_influx.py`](../mqtt_to_influx.py) grava cada leitura em InfluxedB:

- measurement: `consumo`
- tags: `node`, `papel`, `policy`, `run`  ← a tag `policy` (`round_robin` / `energy` / `energy_cooldown`) rotula a curva; `run` identifica a rodada (carimbada no reset) e é o que separa os ensaios no dashboard
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

Ao soltar o botão, o reset inicia: o nó publica via broadcast seu novo `run_id` (o carimbo de tempo do RTC daquele reset, ex. `2026-06-20 14:00:00`) e transmite para toda a malha o scenario escolhido (CHEIO ou ESCALONADO).

### Morte simulada e FND

Quando a bateria medida de um nó atinge **~1%** (limiar `DEATH_THRESHOLD_PCT`), o nó:
- **Para de publicar** MQTT (não envia mais leituras de corrente/bateria).
- **Recusa a liderança** — não se auto-elege; se for eleito por um peer com visão desatualizada, repassa o rodízio para outro nó.
- **Faz step-down se estiver liderando ao morrer** — passa a liderança a um nó **vivo** (via ROTATE) antes de silenciar, para o uplink não morrer junto.

Isso é **comportamento esperado e proposital** — não é um bug, mas a simulação de uma morte real por bateria. O nó não desaparece da memória da malha imediatamente (os peers o expiram por TTL ao pararem de ouvi-lo). O painel FND usa a query que pega o 1º instante em que a bateria de algum nó cai **até** o limiar (`battery <= fnd_thresh`, padrão 5%) — é esse o instante de "morte" registrado para o teste.

### Selecionando a rodada (run_id) no dashboard

A variável `run` (tabela acima) lista automaticamente todos os `run_id` únicos gravados nos últimos 30 dias (ou conforme `range_start`/`range_stop`). Os resets de reset CHEIO e ESCALONADO geram um novo `run_id` que é:
1. Gerado a partir do RTC do nó que disparou o reset (carimbo de tempo).
2. Transmitido via broadcast para toda a malha (todos os nós passam a usar o mesmo `run_id`).
3. Gravado no NVS de cada nó e publicado em cada leitura MQTT (tag `run`).
4. Ingerido pelo bridge [`../mqtt_to_influx.py`](../mqtt_to_influx.py) na tag `run` do InfluxDB.

Para **isolar um ensaio específico** no dashboard:
- Selecione o(s) `run_id` no dropdown `run` (aparece como o carimbo de tempo da rodada). É **multi-seleção**: marque um `run` por política para sobrepor as curvas.
- Se várias rodadas existem no mesmo horário, use `range_start`/`range_stop` para estreitar a janela real (ex. `2026-06-20T14:00:00Z` … `2026-06-20T14:30:00Z`).

**Comparando as 3 políticas (round_robin / energy / energy_cooldown):** rode os 3 ensaios do mesmo cenário (cada um gera seu `run_id`) e, no dashboard, marque os **3 `run_id`** no dropdown `run`. As 3 curvas aparecem sobrepostas, cada uma rotulada pela sua política, e o painel FND mostra 3 valores lado a lado.

### Cenário A (CHEIO) vs Cenário B (ESCALONADO) — quando usar

**Cenário A — Reset CHEIO (5–10 s)**
- Todos os 5 nós começam com **100% de bateria**.
- **Homogêneo**: mesmas condições iniciais para as três políticas.
- **Uso na monografia**: comparação "fair" entre as políticas; isola o efeito do algoritmo sem confundidor de estado inicial.
- Exemplo: "round_robin mantém o FND por X min, energy por Y min, energy_cooldown por Z min, partindo de igualdade."

**Cenário B — Reset ESCALONADO (>10 s)**
- Os 5 nós têm bateria inicial: **[100%, 85%, 70%, 55%, 40%]** (por ordem de MAC — com 5 ESPs cada um pega exatamente um nível).
- **Heterogêneo**: simula uma malha que já sofreu desgaste desigual (nós periféricos descarregam mais cedo que centrais em topologias reais).
- **Uso na monografia**: avalia resiliência sob condições de bateria já degradada; mostra se a política consegue "rebalancear" o consumo quando algum nó já chega fraco.
- Exemplo: "Política A degrada-se quando nós periféricos chegam com <60% de bateria, política B mantém a distribuição mesmo com essa heterogeneidade."

**Escolha recomendada:**
- Se o foco é **algoritmo vs algoritmo**, use Cenário A (as três políticas na mesma base).
- Se o foco é **robustez em malhas reais** (que já têm desgaste acumulado), use Cenário B.
- Para cobertura completa na monografia, rode os **6 ensaios** (3 políticas × 2 cenários) e compare os gráficos de FND lado a lado.

## Lógica da query (resumo)

`from() |> range(janela real)` → acha `t0` por **run** (`group by run |> sort |> first`)
→ `join.left` injeta `t0` em cada leitura → `map` reescreve `_time = _time - t0` (epoch) →
nó mais fraco por bucket (`truncateTimeColumn |> group(run,_time) |> min`). O FND filtra
`battery <= fnd_thresh`, pega o `first` por run e converte o offset para minutos. A coluna
`policy` é mantida em cada série só para rotular as curvas (`displayName`).

Queries validadas contra InfluxDB **v2.8.0** (requer o pacote Flux `join`, presente nessa versão).
