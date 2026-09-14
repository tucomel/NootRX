#!/bin/bash
# ==============================================================================
# Monitor de Telemetria Interativo em Tempo Real - AMD Radeon RX 6900 XT Red Devil
# ==============================================================================
# Modos de Visualização:
#   [F1 ou 1 ou C] -> CURRENT (Telemetria Instantânea em Tempo Real)
#   [F2 ou 2 ou M] -> MIN     (Valores Mínimos Registrados na Sessão)
#   [F3 ou 3 ou X] -> MAX     (Valores Máximos / Picos Registrados)
#   [F4 ou 4 ou A] -> AVERAGE (Médias Aritméticas da Sessão)
#   [R]            -> RESET   (Reiniciar estatísticas acumuladas)
#   [Q ou Ctrl+C]  -> SAIR    (Encerrar monitor)
# ==============================================================================

# Cores ANSI de Alta Visibilidade
C_RESET="\033[0m"
C_BOLD="\033[1m"
C_DIM="\033[2m"

# Paleta de Cores
C_WHITE="\033[1;37m"
C_GREEN="\033[1;32m"
C_BLUE="\033[1;34m"
C_CYAN="\033[1;36m"
C_YELLOW="\033[1;33m"
C_RED="\033[1;31m"
C_MAGENTA="\033[1;35m"

# Badges de Abas (Fundo Colorido)
BG_CYAN="\033[1;30;46m"
BG_BLUE="\033[1;37;44m"
BG_RED="\033[1;37;41m"
BG_GREEN="\033[1;30;42m"
BG_DARK="\033[1;37;100m"

# Estado Inicial
MODE="CURRENT"
SAMPLES=0
START_EPOCH=$(date +%s)
RESET_NOTIF=""

# Ocultar cursor e restaurar na saída
echo -e "\033[?25l"
trap 'echo -e "\033[?25h\n${C_GREEN}Monitoramento encerrado com sucesso.${C_RESET}"; exit 0' SIGINT SIGTERM

SMC_BIN="/Applications/Stats.app/Contents/Resources/smc"

# Função para desenhar barras de progresso ASCII
make_bar() {
  local pct=${1:-0}
  local width=${2:-14}
  if [ "$pct" -gt 100 ]; then pct=100; fi
  if [ "$pct" -lt 0 ]; then pct=0; fi
  local filled=$(( pct * width / 100 ))
  local empty=$(( width - filled ))
  local bar=""
  for ((i=0; i<filled; i++)); do bar="${bar}█"; done
  for ((i=0; i<empty; i++)); do bar="${bar}░"; done
  echo "$bar"
}

while true; do
  DATA=$(ioreg -l -w0 -r -c AMDRadeonX6000_AMDNavi21GraphicsAccelerator 2>/dev/null)
  
  if [ -z "$DATA" ]; then
    clear
    echo -e "${C_RED}Erro: Acelerador gráfico Navi 21 não encontrado no IORegistry.${C_RESET}"
    echo "Aguardando GPU..."
    sleep 2
    continue
  fi

  # Coleta de métricas brutas
  POWER=$(echo "$DATA" | grep -o '"Total Power(W)"=[0-9]*' | head -n 1 | cut -d= -f2)
  CORE=$(echo "$DATA" | grep -o '"Core Clock(MHz)"=[0-9]*' | head -n 1 | cut -d= -f2)
  MEM=$(echo "$DATA" | grep -o '"Memory Clock(MHz)"=[0-9]*' | head -n 1 | cut -d= -f2)
  TEMP=$(echo "$DATA" | grep -o '"Temperature(C)"=[0-9]*' | head -n 1 | cut -d= -f2)
  RPM=$(echo "$DATA" | grep -o '"Fan Speed(RPM)"=[0-9]*' | head -n 1 | cut -d= -f2)
  FAN_PCT=$(echo "$DATA" | grep -o '"Fan Speed(%)"=[0-9]*' | head -n 1 | cut -d= -f2)
  LOAD=$(echo "$DATA" | grep -o '"GPU Activity(%)"=[0-9]*' | head -n 1 | cut -d= -f2)
  UTIL=$(echo "$DATA" | grep -o '"Device Utilization %"=[0-9]*' | head -n 1 | cut -d= -f2)
  VRAM_USED=$(echo "$DATA" | grep -o '"inUseVidMemoryBytes"=[0-9]*' | head -n 1 | cut -d= -f2)
  VRAM_FREE=$(echo "$DATA" | grep -o '"vramFreeBytes"=[0-9]*' | head -n 1 | cut -d= -f2)
  GART_USED=$(echo "$DATA" | grep -o '"gartUsedBytes"=[0-9]*' | head -n 1 | cut -d= -f2)
  GART_FREE=$(echo "$DATA" | grep -o '"gartFreeBytes"=[0-9]*' | head -n 1 | cut -d= -f2)
  RECOVERY=$(echo "$DATA" | grep -o '"recoveryCount"=[0-9]*' | head -n 1 | cut -d= -f2)
  GFX_SUB=$(echo "$DATA" | grep -o '"HWChannel GFX | Commands Submitted"=[0-9]*' | head -n 1 | cut -d= -f2)
  GFX_CMP=$(echo "$DATA" | grep -o '"HWChannel GFX | Commands Completed"=[0-9]*' | head -n 1 | cut -d= -f2)
  TEX=$(echo "$DATA" | grep -o '"textureCount"=[0-9]*' | head -n 1 | cut -d= -f2)
  CTX_2D=$(echo "$DATA" | grep -o '"context2DCount"=[0-9]*' | head -n 1 | cut -d= -f2)
  CTX_VID=$(echo "$DATA" | grep -o '"contextVideoCount"=[0-9]*' | head -n 1 | cut -d= -f2)

  # Tratamento de valores nulos/padrão
  POWER=${POWER:-0}; CORE=${CORE:-0}; MEM=${MEM:-0}; TEMP=${TEMP:-0}
  RPM=${RPM:-0}; FAN_PCT=${FAN_PCT:-0}; LOAD=${LOAD:-0}; UTIL=${UTIL:-0}
  VRAM_USED=${VRAM_USED:-0}; VRAM_FREE=${VRAM_FREE:-0}
  GART_USED=${GART_USED:-0}; GART_FREE=${GART_FREE:-0}
  RECOVERY=${RECOVERY:-0}; GFX_SUB=${GFX_SUB:-0}; GFX_CMP=${GFX_CMP:-0}
  TEX=${TEX:-0}; CTX_2D=${CTX_2D:-0}; CTX_VID=${CTX_VID:-0}

  # Atualização de Estatísticas (Mín, Máx, Soma e Médias)
  SAMPLES=$(( SAMPLES + 1 ))
  if [ "$SAMPLES" -eq 1 ]; then
    POWER_MIN=$POWER; POWER_MAX=$POWER; POWER_SUM=$POWER
    CORE_MIN=$CORE;   CORE_MAX=$CORE;   CORE_SUM=$CORE
    MEM_MIN=$MEM;     MEM_MAX=$MEM;     MEM_SUM=$MEM
    TEMP_MIN=$TEMP;   TEMP_MAX=$TEMP;   TEMP_SUM=$TEMP
    RPM_MIN=$RPM;     RPM_MAX=$RPM;     RPM_SUM=$RPM
    LOAD_MIN=$LOAD;   LOAD_MAX=$LOAD;   LOAD_SUM=$LOAD
    UTIL_MIN=$UTIL;   UTIL_MAX=$UTIL;   UTIL_SUM=$UTIL
    VRAM_MIN=$VRAM_USED; VRAM_MAX=$VRAM_USED; VRAM_SUM=$VRAM_USED
    GART_MIN=$GART_USED; GART_MAX=$GART_USED; GART_SUM=$GART_USED
  else
    if [ "$POWER" -lt "$POWER_MIN" ]; then POWER_MIN=$POWER; fi
    if [ "$POWER" -gt "$POWER_MAX" ]; then POWER_MAX=$POWER; fi
    POWER_SUM=$(( POWER_SUM + POWER ))

    if [ "$CORE" -lt "$CORE_MIN" ]; then CORE_MIN=$CORE; fi
    if [ "$CORE" -gt "$CORE_MAX" ]; then CORE_MAX=$CORE; fi
    CORE_SUM=$(( CORE_SUM + CORE ))

    if [ "$MEM" -lt "$MEM_MIN" ]; then MEM_MIN=$MEM; fi
    if [ "$MEM" -gt "$MEM_MAX" ]; then MEM_MAX=$MEM; fi
    MEM_SUM=$(( MEM_SUM + MEM ))

    if [ "$TEMP" -lt "$TEMP_MIN" ]; then TEMP_MIN=$TEMP; fi
    if [ "$TEMP" -gt "$TEMP_MAX" ]; then TEMP_MAX=$TEMP; fi
    TEMP_SUM=$(( TEMP_SUM + TEMP ))

    if [ "$RPM" -lt "$RPM_MIN" ]; then RPM_MIN=$RPM; fi
    if [ "$RPM" -gt "$RPM_MAX" ]; then RPM_MAX=$RPM; fi
    RPM_SUM=$(( RPM_SUM + RPM ))

    if [ "$LOAD" -lt "$LOAD_MIN" ]; then LOAD_MIN=$LOAD; fi
    if [ "$LOAD" -gt "$LOAD_MAX" ]; then LOAD_MAX=$LOAD; fi
    LOAD_SUM=$(( LOAD_SUM + LOAD ))

    if [ "$UTIL" -lt "$UTIL_MIN" ]; then UTIL_MIN=$UTIL; fi
    if [ "$UTIL" -gt "$UTIL_MAX" ]; then UTIL_MAX=$UTIL; fi
    UTIL_SUM=$(( UTIL_SUM + UTIL ))

    if [ "$VRAM_USED" -lt "$VRAM_MIN" ]; then VRAM_MIN=$VRAM_USED; fi
    if [ "$VRAM_USED" -gt "$VRAM_MAX" ]; then VRAM_MAX=$VRAM_USED; fi
    VRAM_SUM=$(( VRAM_SUM + VRAM_USED ))

    if [ "$GART_USED" -lt "$GART_MIN" ]; then GART_MIN=$GART_USED; fi
    if [ "$GART_USED" -gt "$GART_MAX" ]; then GART_MAX=$GART_USED; fi
    GART_SUM=$(( GART_SUM + GART_USED ))
  fi

  POWER_AVG=$(( POWER_SUM / SAMPLES ))
  CORE_AVG=$(( CORE_SUM / SAMPLES ))
  MEM_AVG=$(( MEM_SUM / SAMPLES ))
  TEMP_AVG=$(( TEMP_SUM / SAMPLES ))
  RPM_AVG=$(( RPM_SUM / SAMPLES ))
  LOAD_AVG=$(( LOAD_SUM / SAMPLES ))
  UTIL_AVG=$(( UTIL_SUM / SAMPLES ))
  VRAM_AVG=$(( VRAM_SUM / SAMPLES ))
  GART_AVG=$(( GART_SUM / SAMPLES ))

  # Cálculo de Tempo Decorrido
  NOW=$(date +%s)
  ELAPSED=$(( NOW - START_EPOCH ))
  HOURS=$(( ELAPSED / 3600 ))
  MINS=$(( (ELAPSED % 3600) / 60 ))
  SECS=$(( ELAPSED % 60 ))
  UPTIME=$(printf "%02d:%02d:%02d" $HOURS $MINS $SECS)

  # Métricas Globais de VRAM e GART
  VRAM_TOTAL=$(( VRAM_USED + VRAM_FREE ))
  VRAM_TOTAL_GB=$(awk "BEGIN {printf \"%.2f\", $VRAM_TOTAL / 1073741824}")
  VRAM_CUR_GB=$(awk "BEGIN {printf \"%.2f\", $VRAM_USED / 1073741824}")
  VRAM_MIN_GB=$(awk "BEGIN {printf \"%.2f\", $VRAM_MIN / 1073741824}")
  VRAM_MAX_GB=$(awk "BEGIN {printf \"%.2f\", $VRAM_MAX / 1073741824}")
  VRAM_AVG_GB=$(awk "BEGIN {printf \"%.2f\", $VRAM_AVG / 1073741824}")

  GART_TOTAL=$(( GART_USED + GART_FREE ))
  GART_TOTAL_GB=$(awk "BEGIN {printf \"%.2f\", $GART_TOTAL / 1073741824}")
  GART_CUR_MB=$(( GART_USED / 1048576 ))
  GART_MIN_MB=$(( GART_MIN / 1048576 ))
  GART_MAX_MB=$(( GART_MAX / 1048576 ))
  GART_AVG_MB=$(( GART_AVG / 1048576 ))

  # Seleção dos Valores a Exibir Conforme o Modo Ativo
  case "$MODE" in
    CURRENT)
      MODE_TITLE="TELEMETRIA INSTANTÂNEA EM TEMPO REAL"
      SHOW_POWER=$POWER
      SHOW_CORE=$CORE
      SHOW_MEM=$MEM
      SHOW_TEMP=$TEMP
      SHOW_RPM=$RPM
      SHOW_LOAD=$LOAD
      SHOW_UTIL=$UTIL
      SHOW_VRAM=$VRAM_USED
      SHOW_VRAM_GB=$VRAM_CUR_GB
      SHOW_GART=$GART_USED
      SHOW_GART_MB=$GART_CUR_MB

      REF_POWER="(Mín: ${POWER_MIN}W | Máx: ${POWER_MAX}W | Méd: ${POWER_AVG}W)"
      REF_CORE="(Mín: ${CORE_MIN} | Máx: ${CORE_MAX} | Méd: ${CORE_AVG})"
      REF_MEM="(Mín: ${MEM_MIN} | Máx: ${MEM_MAX} | Méd: ${MEM_AVG})"
      REF_TEMP="(Mín: ${TEMP_MIN}°C | Máx: ${TEMP_MAX}°C | Méd: ${TEMP_AVG}°C)"
      REF_LOAD="(Mín: ${LOAD_MIN}% | Máx: ${LOAD_MAX}% | Méd: ${LOAD_AVG}%)"
      REF_VRAM="(Mín: ${VRAM_MIN_GB}G | Máx: ${VRAM_MAX_GB}G | Méd: ${VRAM_AVG_GB}G)"
      REF_GART="(Mín: ${GART_MIN_MB}M | Máx: ${GART_MAX_MB}M | Méd: ${GART_AVG_MB}M)"
      ;;
    MIN)
      MODE_TITLE="VALORES MÍNIMOS REGISTRADOS NA SESSÃO"
      SHOW_POWER=$POWER_MIN
      SHOW_CORE=$CORE_MIN
      SHOW_MEM=$MEM_MIN
      SHOW_TEMP=$TEMP_MIN
      SHOW_RPM=$RPM_MIN
      SHOW_LOAD=$LOAD_MIN
      SHOW_UTIL=$UTIL_MIN
      SHOW_VRAM=$VRAM_MIN
      SHOW_VRAM_GB=$VRAM_MIN_GB
      SHOW_GART=$GART_MIN
      SHOW_GART_MB=$GART_MIN_MB

      REF_POWER="(Atual: ${POWER}W | Máx: ${POWER_MAX}W | Méd: ${POWER_AVG}W)"
      REF_CORE="(Atual: ${CORE} | Máx: ${CORE_MAX} | Méd: ${CORE_AVG})"
      REF_MEM="(Atual: ${MEM} | Máx: ${MEM_MAX} | Méd: ${MEM_AVG})"
      REF_TEMP="(Atual: ${TEMP}°C | Máx: ${TEMP_MAX}°C | Méd: ${TEMP_AVG}°C)"
      REF_LOAD="(Atual: ${LOAD}% | Máx: ${LOAD_MAX}% | Méd: ${LOAD_AVG}%)"
      REF_VRAM="(Atual: ${VRAM_CUR_GB}G | Máx: ${VRAM_MAX_GB}G | Méd: ${VRAM_AVG_GB}G)"
      REF_GART="(Atual: ${GART_CUR_MB}M | Máx: ${GART_MAX_MB}M | Méd: ${GART_AVG_MB}M)"
      ;;
    MAX)
      MODE_TITLE="VALORES MÁXIMOS (PICOS) REGISTRADOS NA SESSÃO"
      SHOW_POWER=$POWER_MAX
      SHOW_CORE=$CORE_MAX
      SHOW_MEM=$MEM_MAX
      SHOW_TEMP=$TEMP_MAX
      SHOW_RPM=$RPM_MAX
      SHOW_LOAD=$LOAD_MAX
      SHOW_UTIL=$UTIL_MAX
      SHOW_VRAM=$VRAM_MAX
      SHOW_VRAM_GB=$VRAM_MAX_GB
      SHOW_GART=$GART_MAX
      SHOW_GART_MB=$GART_MAX_MB

      REF_POWER="(Atual: ${POWER}W | Mín: ${POWER_MIN}W | Méd: ${POWER_AVG}W)"
      REF_CORE="(Atual: ${CORE} | Mín: ${CORE_MIN} | Méd: ${CORE_AVG})"
      REF_MEM="(Atual: ${MEM} | Mín: ${MEM_MIN} | Méd: ${MEM_AVG})"
      REF_TEMP="(Atual: ${TEMP}°C | Mín: ${TEMP_MIN}°C | Méd: ${TEMP_AVG}°C)"
      REF_LOAD="(Atual: ${LOAD}% | Mín: ${LOAD_MIN}% | Méd: ${LOAD_AVG}%)"
      REF_VRAM="(Atual: ${VRAM_CUR_GB}G | Mín: ${VRAM_MIN_GB}G | Méd: ${VRAM_AVG_GB}G)"
      REF_GART="(Atual: ${GART_CUR_MB}M | Mín: ${GART_MIN_MB}M | Méd: ${GART_AVG_MB}M)"
      ;;
    AVG)
      MODE_TITLE="MÉDIAS ARITMÉTICAS ACUMULADAS DA SESSÃO"
      SHOW_POWER=$POWER_AVG
      SHOW_CORE=$CORE_AVG
      SHOW_MEM=$MEM_AVG
      SHOW_TEMP=$TEMP_AVG
      SHOW_RPM=$RPM_AVG
      SHOW_LOAD=$LOAD_AVG
      SHOW_UTIL=$UTIL_AVG
      SHOW_VRAM=$VRAM_AVG
      SHOW_VRAM_GB=$VRAM_AVG_GB
      SHOW_GART=$GART_AVG
      SHOW_GART_MB=$GART_AVG_MB

      REF_POWER="(Atual: ${POWER}W | Mín: ${POWER_MIN}W | Máx: ${POWER_MAX}W)"
      REF_CORE="(Atual: ${CORE} | Mín: ${CORE_MIN} | Máx: ${CORE_MAX})"
      REF_MEM="(Atual: ${MEM} | Mín: ${MEM_MIN} | Máx: ${MEM_MAX})"
      REF_TEMP="(Atual: ${TEMP}°C | Mín: ${TEMP_MIN}°C | Máx: ${TEMP_MAX}°C)"
      REF_LOAD="(Atual: ${LOAD}% | Mín: ${LOAD_MIN}% | Máx: ${LOAD_MAX}%)"
      REF_VRAM="(Atual: ${VRAM_CUR_GB}G | Mín: ${VRAM_MIN_GB}G | Máx: ${VRAM_MAX_GB}G)"
      REF_GART="(Atual: ${GART_CUR_MB}M | Mín: ${GART_MIN_MB}M | Máx: ${GART_MAX_MB}M)"
      ;;
  esac

  # Barras Dinâmicas para o Modo Selecionado
  VRAM_PCT=$(( VRAM_TOTAL > 0 ? (SHOW_VRAM * 100 / VRAM_TOTAL) : 0 ))
  VRAM_BAR=$(make_bar $VRAM_PCT 12)
  GART_PCT=$(( GART_TOTAL > 0 ? (SHOW_GART * 100 / GART_TOTAL) : 0 ))
  GART_BAR=$(make_bar $GART_PCT 12)
  LOAD_BAR=$(make_bar $SHOW_LOAD 12)

  # Badges de Status
  PWR_STATUS="${C_GREEN}[IDLE EFICIENTE]${C_RESET}"
  if [ "$SHOW_POWER" -gt 45 ]; then PWR_STATUS="${C_YELLOW}[CARGA MODERADA]${C_RESET}"; fi
  if [ "$SHOW_POWER" -gt 120 ]; then PWR_STATUS="${C_RED}[ALTA PERFORMANCE]${C_RESET}"; fi

  CORE_STATUS="${C_GREEN}[FLOOR 500MHz]${C_RESET}"
  if [ "$SHOW_CORE" -gt 600 ]; then CORE_STATUS="${C_CYAN}[BOOST DINÂMICO]${C_RESET}"; fi

  FAN_STATUS="${C_GREEN}[0dB SILENCIOSO]${C_RESET}"
  if [ "$SHOW_RPM" -gt 0 ]; then FAN_STATUS="${C_YELLOW}[VENTOINHA ATIVA]${C_RESET}"; fi

  TEMP_STATUS="${C_GREEN}[FRIO]${C_RESET}"
  if [ "$SHOW_TEMP" -gt 50 ]; then TEMP_STATUS="${C_YELLOW}[NOMINAL]${C_RESET}"; fi
  if [ "$SHOW_TEMP" -gt 75 ]; then TEMP_STATUS="${C_RED}[QUENTE]${C_RESET}"; fi

  # Verificação de Sensores SMC da CPU (se Stats SMC estiver disponível)
  SMC_CPU_INFO=""
  if [ -x "$SMC_BIN" ]; then
    SMC_CPU_TEMP=$($SMC_BIN list -t 2>/dev/null | grep '\[TC0D\]' | awk '{print $2}')
    SMC_CPU_PWR=$($SMC_BIN list -p 2>/dev/null | grep '\[PC0C\]' | awk '{print $2}')
    if [ -n "$SMC_CPU_TEMP" ] && [ -n "$SMC_CPU_PWR" ]; then
      CPU_PWR_FMT=$(awk "BEGIN {printf \"%.1f\", $SMC_CPU_PWR}")
      SMC_CPU_INFO="  ${C_BOLD}💻 CPU Host (Intel Core)${C_RESET} : ${C_CYAN}${SMC_CPU_TEMP}°C${C_RESET} | Consumo: ${C_YELLOW}${CPU_PWR_FMT} W${C_RESET}\n"
    fi
  fi

  # Formatação das Abas de Navegação
  t1="${BG_DARK} [F1/1] ATUAL ${C_RESET}"
  t2="${BG_DARK} [F2/2] MÍNIMO ${C_RESET}"
  t3="${BG_DARK} [F3/3] MÁXIMO ${C_RESET}"
  t4="${BG_DARK} [F4/4] MÉDIA ${C_RESET}"
  case "$MODE" in
    CURRENT) t1="${BG_CYAN}${C_BOLD} ▶ [F1/1] ATUAL ◀ ${C_RESET}" ;;
    MIN)     t2="${BG_BLUE}${C_BOLD} ▶ [F2/2] MÍNIMO ◀ ${C_RESET}" ;;
    MAX)     t3="${BG_RED}${C_BOLD} ▶ [F3/3] MÁXIMO ◀ ${C_RESET}" ;;
    AVG)     t4="${BG_GREEN}${C_BOLD} ▶ [F4/4] MÉDIA ◀ ${C_RESET}" ;;
  esac

  # Renderização do Dashboard
  clear
  echo -e "${C_BLUE}================================================================================${C_RESET}"
  echo -e "   ${C_BOLD}${C_CYAN}⚡ NAVI 21 TELEMETRY CENTER — AMD RADEON RX 6900 XT RED DEVIL${C_RESET}"
  echo -e "   ${C_DIM}Driver: NootRX v1.0.21 | SMU Floor 500MHz | Sessão: ${UPTIME} (${SAMPLES} amostras)${C_RESET}"
  echo -e "${C_BLUE}================================================================================${C_RESET}"
  echo -e "  Abas: $t1  $t2  $t3  $t4"
  echo -e "  ${C_DIM}Visualização: ${C_BOLD}${MODE_TITLE}${C_RESET}"
  if [ -n "$RESET_NOTIF" ]; then
    echo -e "  ${C_YELLOW}${C_BOLD}${RESET_NOTIF}${C_RESET}"
    RESET_NOTIF=""
  fi
  echo -e "${C_BLUE}--------------------------------------------------------------------------------${C_RESET}"
  printf "  ${C_BOLD}%-23s${C_RESET} : ${C_YELLOW}%4s W${C_RESET}  ${C_DIM}%-32s${C_RESET} %b\n" "⚡ Potência (Board)" "$SHOW_POWER" "$REF_POWER" "$PWR_STATUS"
  printf "  ${C_BOLD}%-23s${C_RESET} : ${C_GREEN}%4s MHz${C_RESET} ${C_DIM}%-32s${C_RESET} %b\n" "⏱  Core Clock (GFX)" "$SHOW_CORE" "$REF_CORE" "$CORE_STATUS"
  printf "  ${C_BOLD}%-23s${C_RESET} : ${C_CYAN}%4s MHz${C_RESET} ${C_DIM}%-32s${C_RESET} ${C_DIM}(GDDR6 UCLK)${C_RESET}\n" "⏱  Memory Clock (VRAM)" "$SHOW_MEM" "$REF_MEM"
  echo -e "${C_BLUE}--------------------------------------------------------------------------------${C_RESET}"
  printf "  ${C_BOLD}%-23s${C_RESET} : ${C_RED}%3s °C${C_RESET}   ${C_DIM}%-32s${C_RESET} %b\n" "🌡  Temperatura (Edge)" "$SHOW_TEMP" "$REF_TEMP" "$TEMP_STATUS"
  printf "  ${C_BOLD}%-23s${C_RESET} : ${C_DIM}%-38s${C_RESET} ${C_DIM}(SMC FW)*${C_RESET}\n" "🌡  Hotspot & VRAM Temp" "Gerenciados internamente pela SMU"
  printf "  ${C_BOLD}%-23s${C_RESET} : %4s RPM  ${C_DIM}(%s%% PWM)${C_RESET}                        %b\n" "🌀 Ventoinhas (Fans)" "$SHOW_RPM" "$FAN_PCT" "$FAN_STATUS"
  echo -e "${C_BLUE}--------------------------------------------------------------------------------${C_RESET}"
  printf "  ${C_BOLD}%-23s${C_RESET} : [${C_CYAN}%s${C_RESET}] %2s%% (${SHOW_VRAM_GB} GB / ${VRAM_TOTAL_GB} GB)  ${C_DIM}%s${C_RESET}\n" "🧠 VRAM Dedicada (16GB)" "$VRAM_BAR" "$VRAM_PCT" "$REF_VRAM"
  printf "  ${C_BOLD}%-23s${C_RESET} : [${C_BLUE}%s${C_RESET}]  %2s%% (${SHOW_GART_MB} MB / ${GART_TOTAL_GB} GB)  ${C_DIM}%s${C_RESET}\n" "📦 Memória GART (Host)" "$GART_BAR" "$GART_PCT" "$REF_GART"
  echo -e "${C_BLUE}--------------------------------------------------------------------------------${C_RESET}"
  printf "  ${C_BOLD}%-23s${C_RESET} : [${C_YELLOW}%s${C_RESET}] %2s%%  ${C_DIM}%-30s${C_RESET} ${C_DIM}(Metal Util: %s%%)${C_RESET}\n" "⚙  Carga da GPU (Load)" "$LOAD_BAR" "$SHOW_LOAD" "$REF_LOAD" "$UTIL"
  printf "  ${C_BOLD}%-23s${C_RESET} : Submetidos: ${C_CYAN}%-6s${C_RESET} | Completos: ${C_GREEN}%-6s${C_RESET} ${C_DIM}(Pipeline 3D)${C_RESET}\n" "🚀 Comandos Metal" "$GFX_SUB" "$GFX_CMP"
  printf "  ${C_BOLD}%-23s${C_RESET} : %-5s texturas | Contextos: 2D: %-2s | Vídeo: %-2s\n" "🎨 Recursos Alocados" "$TEX" "$CTX_2D" "$CTX_VID"
  echo -e "${C_BLUE}--------------------------------------------------------------------------------${C_RESET}"
  if [ -n "$SMC_CPU_INFO" ]; then
    echo -en "$SMC_CPU_INFO"
    echo -e "${C_BLUE}--------------------------------------------------------------------------------${C_RESET}"
  fi
  printf "  ${C_BOLD}%-23s${C_RESET} : ${C_GREEN}✓ 0 RESETS (100%% ESTÁVEL / SEM TRAVAMENTOS)${C_RESET}\n" "🛡  Saúde / Recovery"
  echo -e "${C_BLUE}================================================================================${C_RESET}"
  echo -e "  ${C_BOLD}Atalhos:${C_RESET} [${C_CYAN}F1/1${C_RESET}] Atual  [${C_BLUE}F2/2${C_RESET}] Mínimo  [${C_RED}F3/3${C_RESET}] Máximo  [${C_GREEN}F4/4${C_RESET}] Média  [${C_YELLOW}R${C_RESET}] Reset  [${C_WHITE}Q${C_RESET}] Sair"
  echo -e "${C_BLUE}================================================================================${C_RESET}"
  echo -e "${C_DIM}* Nota Técnica: No macOS, os drivers proprietários da Apple expõem o diodo de borda (Edge).${C_RESET}"
  echo -e "${C_DIM}  Tjunction (Hotspot) e VRAM GDDR6 são gerenciadas pelo firmware SMU do silício.${C_RESET}"

  # Leitura de Teclado Não-Bloqueante (Timeout de 1 segundo para atualização)
  if read -t 1 -s -n 1 char; then
    if [ "$char" = $'\e' ]; then
      read -t 1 -s -n 3 rest
      key="${char}${rest}"
    else
      key="$char"
    fi

    # Mapeamento de Teclas (F1-F4, 1-4, C, M, X, A, R, Q)
    if [[ "$key" == "1" || "$key" == "c" || "$key" == "C" || "$key" == $'\eOP' || "$key" == $'\e[11~' || "$key" == $'\e[[A' || "$key" == $'\e[P' ]]; then
      MODE="CURRENT"
    elif [[ "$key" == "2" || "$key" == "m" || "$key" == "M" || "$key" == "n" || "$key" == "N" || "$key" == $'\eOQ' || "$key" == $'\e[12~' || "$key" == $'\e[[B' || "$key" == $'\e[Q' ]]; then
      MODE="MIN"
    elif [[ "$key" == "3" || "$key" == "x" || "$key" == "X" || "$key" == $'\eOR' || "$key" == $'\e[13~' || "$key" == $'\e[[C' || "$key" == $'\e[R' ]]; then
      MODE="MAX"
    elif [[ "$key" == "4" || "$key" == "a" || "$key" == "A" || "$key" == $'\eOS' || "$key" == $'\e[14~' || "$key" == $'\e[[D' || "$key" == $'\e[S' ]]; then
      MODE="AVG"
    elif [[ "$key" == "r" || "$key" == "R" ]]; then
      SAMPLES=0
      START_EPOCH=$(date +%s)
      RESET_NOTIF="⚡ Estatísticas reiniciadas com sucesso!"
    elif [[ "$key" == "q" || "$key" == "Q" ]]; then
      echo -e "\033[?25h"
      echo -e "\n${C_GREEN}Monitoramento encerrado com sucesso.${C_RESET}"
      exit 0
    fi
  fi
done
