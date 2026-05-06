#!/bin/bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# ---------------------------------------------------------------------------
# Localiza e carrega o ambiente ESP-IDF
# ---------------------------------------------------------------------------
source_idf() {
    command -v idf.py &>/dev/null && return 0

    local candidates=(
        "${IDF_PATH:-}/export.sh"
        "$HOME/.espressif/v5.5.3/esp-idf/export.sh"
        "$HOME/esp/esp-idf/export.sh"
        "/opt/esp-idf/export.sh"
    )
    for f in "${candidates[@]}"; do
        if [ -f "$f" ]; then
            echo -e "${CYAN}Carregando ESP-IDF de $f${NC}"
            # shellcheck disable=SC1090
            source "$f" > /dev/null 2>&1
            return 0
        fi
    done

    echo -e "${RED}Erro: ESP-IDF não encontrado.${NC}"
    echo "Defina IDF_PATH ou instale em ~/esp/esp-idf"
    exit 1
}

source_idf

# Guarda o caminho do export.sh para repassar aos terminais filhos
IDF_EXPORT=""
for f in "${IDF_PATH:-}/export.sh" "$HOME/.espressif/v5.5.3/esp-idf/export.sh" "$HOME/esp/esp-idf/export.sh" "/opt/esp-idf/export.sh"; do
    [ -f "$f" ] && IDF_EXPORT="$f" && break
done

# ---------------------------------------------------------------------------
# Detecta ESPs conectados
# ---------------------------------------------------------------------------
PORTS=()
for dev in /dev/ttyUSB* /dev/ttyACM*; do
    [ -e "$dev" ] && PORTS+=("$dev")
done

if [ ${#PORTS[@]} -eq 0 ]; then
    echo -e "${RED}Nenhum ESP encontrado em /dev/ttyUSB* ou /dev/ttyACM*${NC}"
    exit 1
fi

echo -e "${GREEN}ESPs encontrados (${#PORTS[@]}):${NC} ${PORTS[*]}"

# ---------------------------------------------------------------------------
# Build (uma vez só)
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo -e "${YELLOW}▶ Compilando firmware...${NC}"
idf.py build
echo -e "${GREEN}✔ Build concluído.${NC}"

# ---------------------------------------------------------------------------
# Flash em paralelo (usa esptool direto para evitar conflito de build lock)
# ---------------------------------------------------------------------------
echo -e "${YELLOW}▶ Flasheando ${#PORTS[@]} ESP(s) em paralelo...${NC}"

flash_one() {
    local PORT="$1"

    if idf.py -p "$PORT" flash 2>&1 | while IFS= read -r line; do echo "[$PORT] $line"; done; then
        echo -e "${GREEN}✔ [$PORT] Flash OK${NC}"
        return 0
    else
        echo -e "${RED}✘ [$PORT] Flash FALHOU${NC}"
        return 1
    fi
}

PIDS=()
for PORT in "${PORTS[@]}"; do
    flash_one "$PORT" &
    PIDS+=($!)
done

FAILED=0
for i in "${!PIDS[@]}"; do
    wait "${PIDS[$i]}" || { echo -e "${RED}Falha em ${PORTS[$i]}${NC}"; FAILED=1; }
done

if [ "$FAILED" -ne 0 ]; then
    echo -e "${RED}Um ou mais ESPs falharam. Abortando monitores.${NC}"
    exit 1
fi

echo -e "${GREEN}✔ Flash concluído em todos os ESPs!${NC}"

# ---------------------------------------------------------------------------
# Abre um terminal por ESP para monitoramento
# ---------------------------------------------------------------------------
echo -e "${YELLOW}▶ Abrindo monitores...${NC}"

# Detecta o emulador de terminal a usar.
# Prioridade: variável de ambiente TERMINAL → auto-detecção.
detect_terminal() {
    if [ -n "${TERMINAL:-}" ] && command -v "$TERMINAL" &>/dev/null; then
        echo "$TERMINAL"
        return
    fi
    for t in kitty alacritty gnome-terminal konsole xfce4-terminal mate-terminal lxterminal xterm; do
        command -v "$t" &>/dev/null && echo "$t" && return
    done
    echo ""
}

TERM_BIN="$(detect_terminal)"

if [ -z "$TERM_BIN" ]; then
    echo -e "${RED}Nenhum terminal gráfico encontrado.${NC}"
    echo "Defina a variável TERMINAL ou instale xterm."
    echo "Para monitorar manualmente: idf.py -p <porta> monitor"
    exit 1
fi

echo -e "${CYAN}Usando terminal: $TERM_BIN${NC}"

# Diretório de logs (criado automaticamente, ignorado pelo git)
LOG_DIR="$SCRIPT_DIR/logs"
mkdir -p "$LOG_DIR"

open_monitor() {
    local PORT="$1"
    local PORT_NAME
    PORT_NAME="$(basename "$PORT")"
    local LOG_FILE="$LOG_DIR/${PORT_NAME}.log"
    local TITLE="ESP Monitor — $PORT"

    # Terminal mostra saída colorida; arquivo recebe saída sem códigos ANSI.
    # tee >(sed ...) mantém as cores no terminal e salva limpo no arquivo.
    local CMD
    CMD="$(printf \
        'source %q 2>/dev/null; idf.py -p %q monitor 2>&1 | tee >( sed "s/\x1b\[[0-9;]*[mGKHF]//g" > %q ); echo; echo "Monitor encerrado. Pressione Enter para fechar."; read' \
        "$IDF_EXPORT" "$PORT" "$LOG_FILE")"

    case "$TERM_BIN" in
        kitty)
            kitty --title "$TITLE" -- bash -c "$CMD" ;;
        alacritty)
            alacritty --title "$TITLE" -e bash -c "$CMD" ;;
        gnome-terminal)
            gnome-terminal --title "$TITLE" -- bash -c "$CMD" ;;
        konsole)
            konsole --new-tab --title "$TITLE" -e bash -c "$CMD" ;;
        xfce4-terminal)
            xfce4-terminal --title "$TITLE" -e "bash -c '$CMD'" ;;
        mate-terminal)
            mate-terminal --title "$TITLE" -e "bash -c '$CMD'" ;;
        lxterminal)
            lxterminal --title "$TITLE" -e "bash -c '$CMD'" ;;
        *)
            "$TERM_BIN" -title "$TITLE" -e bash -c "$CMD" ;;
    esac &

    echo -e "${CYAN}  Log: $LOG_FILE${NC}"
}

for PORT in "${PORTS[@]}"; do
    open_monitor "$PORT"
    sleep 0.3
done

echo -e "${GREEN}✔ Pronto! Ctrl+] para sair de cada monitor.${NC}"
echo -e "${CYAN}Dica: defina TERMINAL=<emulador> para forçar um terminal específico.${NC}"
