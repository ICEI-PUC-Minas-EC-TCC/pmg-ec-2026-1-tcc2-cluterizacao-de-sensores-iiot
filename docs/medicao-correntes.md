# Roteiro — medição das correntes por papel (laboratório)

Objetivo: capturar a corrente real de cada nó em cada papel (líder / membro /
ocioso) para calibrar o perfil de energia da simulação (`analysis/`). A captura
é automática: o firmware imprime uma linha `CALIB` por segundo e o `flash_all.sh`
salva o log limpo de cada nó em `logs/`.

## Pré-requisitos
- Linux com ESP-IDF instalado (o `flash_all.sh` usa `/dev/ttyUSB*` e bash).
- Firmware com a linha `CALIB` (Task 1 deste plano) já no código.
- 3 ou mais ESPs com o INA219 ligado, alimentados pela bateria/fonte de medição.

## Passo 1 — Flashar todos os nós
```
./flash_all.sh --backend ina219 --policy round_robin
```
- `--backend ina219`: usa o sensor INA219 (medição por I2C).
- `--policy round_robin`: rotação previsível — todo nó vira líder na vez dele.
- O script abre um monitor por nó e salva `logs/ttyUSB0.log`, `logs/ttyUSB1.log`, …

## Passo 2 — Conferir o boot
Em cada monitor, confirmar:
- `AMMETER_INA219: INA219 OK (...)` — sensor inicializado.
- Linhas `CALIB: role=... I=...mA bat=...%` aparecendo ~1×/s.

## Passo 3 — Capturar o cluster (líder + membro)
Deixar os 3+ nós rodando juntos por **≥ 2 mandatos completos** (~3 minutos; cada
mandato de liderança dura 60 s). Isso garante que cada nó passe por **líder** e
por **membro** com a rotação. Não precisa anotar nada à mão — os logs guardam tudo.

## Passo 4 — Capturar o ocioso
Desligar todos menos **um** nó (ou ligar só um). Sozinho, ele fica ~60 s em
`role=IDLE` (sem líder, Wi-Fi STA desligado) antes de reiniciar. Deixar rodar
~60 s. Pode repetir em 1–2 nós diferentes.

## Passo 5 — Trazer os logs
Copiar a pasta **`logs/`** inteira. É só disso que precisamos.

## Depois (feito por nós, fora do laboratório)
```
python -m analysis.calibration logs/
```
Isso imprime média/desvio/contagem por papel e as 3 linhas prontas para colar em
`analysis/calibration.py` (`LEADER_MA`, `MEMBER_MA`, `IDLE_MA`).

## Cuidados
- `[LEADER] Published:` é a corrente **do próprio líder**; `[LEADER] Member
  reading:` é a corrente **de um membro** repassada pelo líder — não confundir.
  A linha `CALIB` já resolve isso (cada amostra vem com o papel certo).
- Deixar o filtro estabilizar: ignorar os primeiros segundos após cada troca de
  papel (o parser já descarta esse transiente com `--settle-ms`).
