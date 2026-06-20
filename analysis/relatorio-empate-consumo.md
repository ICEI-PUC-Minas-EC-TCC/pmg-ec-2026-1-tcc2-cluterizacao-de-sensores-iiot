# O empate de consumo entre as três políticas — ensaios 2026-06-20

**Conclusão em uma frase:** trocar a política de eleição de líder
(`round_robin`, `energy`, `energy_cooldown`) **não muda quanto a rede consome**.
A corrente por nó é praticamente idêntica nas três políticas e nos dois cenários
(~108–112 mA/nó, variando só ~3 % — o mesmo ruído que existe entre nós).

> Este é o resultado central dos ensaios. Diferente da comparação por tempo de
> vida (FND), que ficou inconclusiva por problemas de coleta, o empate de consumo
> é **robusto**: vem de uma medição direta de corrente, não depende de modelo de
> bateria nem de quais nós sobreviveram.

## A evidência do empate

### 1. Corrente média por nó — o número que importa

Como os ensaios têm nº de nós diferente (5 ou 4), a comparação justa é **por nó**.
Normalizado assim, o empate é gritante:

| Cenário | round_robin | energy | energy_cooldown | Amplitude |
|---|---|---|---|---|
| **A** (CHEIO 100 %) | 109,2 mA | 107,4 mA | 110,7 mA | **3,3 mA (3,0 %)** |
| **B** (ESCALONADO) | 108,5 mA | 109,3 mA | 111,5 mA | **3,1 mA (2,8 %)** |

A diferença entre a política que mais e a que menos consome é ~3 mA sobre uma base
de ~109 mA. Para referência, a variação de corrente **entre nós do mesmo ensaio**
já é dessa ordem (hardware/medição) — ou seja, o efeito da política está **dentro
do ruído**. Não há vencedor.

### 2. Por que empatam: liderar custa o mesmo que ser membro

A causa raiz está no consumo por papel:

| Papel | Corrente média |
|---|---|
| LEADER | 108,0 mA |
| MEMBER | 107,3 mA |

Dentro do mesmo nó, a diferença LEADER−MEMBER fica entre **−6 e +5 mA** sobre ~108 mA
(sem sinal consistente). O rádio sempre ligado (APSTA + SoftAP para o ESP-NOW,
`WIFI_PS_NONE`) domina o consumo nos dois papéis; o STA+MQTT extra do líder é
marginal. **Se o papel quase não muda a corrente, redistribuir o papel (que é tudo
o que as políticas fazem) não muda o consumo da rede.** O empate da seção 1 é a
consequência direta disso.

### 3. As curvas confirmam visualmente

- **Corrente total da rede** (`consumo_total_cenario_A.png`): as três políticas
  ficam **planas e sobrepostas** ao longo de todo o ensaio. Os patamares (~545 mA com
  5 nós, ~445 mA com 4 nós) refletem só o nº de nós, não a política.
- **Corrente acumulada por nó** (`consumo_acumulado_<policy>_<cen>.png`): as curvas
  dos nós sobem em retas praticamente coincidentes (~17 000 mA somados/nó no cenário
  A), sem nenhuma política se descolar.

## O que o empate NÃO quer dizer

- **Não é falha do firmware.** A eleição e o rodízio funcionam (a liderança fica a
  6–10 pp do ideal 1/N nas três políticas) e o split-brain foi corrigido. O empate é
  um resultado físico do hardware, não um bug.
- **Não invalida as políticas em geral** — só mostra que, **neste hardware**, com o
  rádio sempre ativo, a vantagem energética da rotação não aparece. Em uma plataforma
  onde o papel de líder custasse bem mais que o de membro, a rotação separaria as
  políticas.

## Implicação para a monografia

O achado vendável não é "a política X ganhou", e sim: **o gargalo de energia é o rádio
sempre ligado, não a escolha do líder.** Para o rodízio mostrar economia seria preciso
**diferenciar o custo do papel** — p.ex. derrubar o SoftAP ou usar power-save quando o
nó é MEMBER. Sem isso, as três políticas empatam em consumo por construção.

## Próximos passos

O objetivo de todos eles é o mesmo: **fazer o papel de MEMBER custar menos que o de
LEADER**, para que a rotação passe a redistribuir energia de verdade e as políticas
deixem de empatar. Em ordem aproximada de esforço × ganho:

1. **Power-save do Wi-Fi quando MEMBER** (mais barato). Hoje o STA roda com
   `WIFI_PS_NONE` (rádio 100 % ligado). Trocar para `WIFI_PS_MIN_MODEM` /
   `WIFI_PS_MAX_MODEM` no nó que não é líder deixa o modem dormir entre beacons.
   Ganho esperado de dezenas de mA sem mudar a topologia.

2. **Derrubar o SoftAP quando MEMBER.** O SoftAP (necessário para o ESP-NOW em APSTA)
   mantém o rádio ativo o tempo todo. Se só o líder precisa anunciar/coordenar, o
   membro pode descer o AP e subir de volta ao virar líder. Ataca diretamente a causa
   do empate (rádio sempre ligado nos dois papéis).

3. **Light sleep entre transmissões.** Para o MEMBER, que só amostra e envia
   periodicamente, entrar em *light sleep* (RAM preservada, wake rápido) entre os
   envios corta o consumo nos intervalos ociosos mantendo a associação Wi-Fi.

4. **Deep sleep com duty cycling.** Passo mais agressivo: o MEMBER dorme em *deep
   sleep* e acorda em janelas combinadas para amostrar/transmitir. Exige
   re-sincronizar a descoberta e a eleição no wake (o nó "some" da rede enquanto
   dorme) e repensar como o líder coleta dos membros adormecidos — mas é o que dá a
   maior economia e onde a política de rotação mais importaria.

5. **Reduzir potência de TX e cadência de amostragem.** Ajustes ortogonais
   (`esp_wifi_set_max_tx_power`, intervalo de leitura maior) que baixam a base de
   consumo dos dois papéis — úteis para esticar o tempo de vida independentemente da
   política.

6. **Re-rodar os ensaios após o item 1 ou 2 e medir de novo.** Repetir esta análise de
   consumo (corrente por papel e por nó). A hipótese a testar: com o custo do papel
   diferenciado, o empate se quebra e as políticas de eleição por energia
   (`energy` / `energy_cooldown`) passam a estender o FND da rede frente ao
   `round_robin`.

> Metodologia: antes de comparar políticas de novo, fechar também os problemas de
> coleta listados no [relatório completo](relatorio-ensaios-2026-06-20.md)
> (garantir NTP antes do reset, confirmar o reset nos 5 nós, deixar o cenário B
> chegar ao FND) — senão a comparação volta a ser inconclusiva por outro motivo.

---

*Fonte: InfluxDB `meu_tcc`/`dados_esps` (tags `policy`/`run`). Script de consumo:
[`analyze_consumo.py`](analyze_consumo.py). Detalhes completos dos 6 ensaios e
problemas de coleta em [`relatorio-ensaios-2026-06-20.md`](relatorio-ensaios-2026-06-20.md).*
