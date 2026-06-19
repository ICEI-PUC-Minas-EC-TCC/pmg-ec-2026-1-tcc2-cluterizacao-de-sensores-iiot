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

**Importante para isolar um único ensaio:** se você rodar vários testes da mesma política ao
longo de semanas, `-30d` pega todos e o `t0` vira o do primeiro. Para cercar um ensaio
específico, ajuste `range_start`/`range_stop` para a janela real daquele dia (ex.
`2026-06-20T14:00:00Z` … `2026-06-20T22:00:00Z`).

## De onde vêm os dados

O bridge [`../mqtt_to_influx.py`](../mqtt_to_influx.py) grava cada leitura em InfluxedB:

- measurement: `consumo`
- tags: `node`, `papel`, `policy`  ← a tag `policy` (`energy` / `round_robin`) é o que separa os ensaios
- fields: `current_ma`, `battery_pct`

> A tag `policy` só existe em dados gravados pela versão atual do bridge. Garanta que o
> firmware publica `policy` no JSON e que cada ensaio roda com a sua política.

## Como reproduzir o ensaio

1. Suba o ESP com a política A; rode o bridge; deixe a malha descarregar até o 1º nó morrer.
2. Repita com a política B (outro horário).
3. Abra o dashboard. As duas curvas aparecem sobrepostas em tempo decorrido e o painel de
   FND mostra os minutos de cada uma.

## Lógica da query (resumo)

`from() |> range(janela real)` → acha `t0` por política (`group by policy |> sort |> first`)
→ `join.left` injeta `t0` em cada leitura → `map` reescreve `_time = _time - t0` (epoch) →
nó mais fraco por bucket (`truncateTimeColumn |> group(policy,_time) |> min`). O FND filtra
`battery <= fnd_thresh`, pega o `first` por política e converte o offset para minutos.

Queries validadas contra InfluxDB **v2.8.0** (requer o pacote Flux `join`, presente nessa versão).
