#!/bin/bash
# ==============================================================================
# Monitor de Telemetria em Tempo Real Avançado - AMD Radeon RX 6900 XT Red Devil
# ==============================================================================
# Suporte: macOS Sequoia / Sonoma / Ventura | Driver: NootRX v1.0.21
# ==============================================================================

# Cores ANSI
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
RED='\033[0;31m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m'

# Função para desenhar barra de progresso em texto
make_bar() {
  local pct=${1:-0}
  local width=${2:-18}
  if [ "$pct" -gt 100 ]; then pct=100; fi
  if [ "$pct" -lt 0 ]; then pct=0; fi
  local filled=$(( pct * width / 100 ))
  local empty=$(( width - filled ))
  local bar=""
  for ((i=0; i<filled; i++)); do bar="${bar}█"; done
  for ((i=0; i<empty; i++)); do bar="${bar}░"; done
  echo "$bar"
}

# Captura Ctrl+C para sair restaurando o terminal
trap 'echo -e "\n${GREEN}Monitoramento encerrado com sucesso.${NC}"; exit 0' SIGINT SIGTERM

SMC_BIN="/Applications/Stats.app/Contents/Resources/smc"

while true; do
  DATA=$(ioreg -l -w0 -r -c AMDRadeonX6000_AMDNavi21GraphicsAccelerator 2>/dev/null)
  
  if [ -z "$DATA" ]; then
    clear
    echo -e "${RED}Erro: Acelerador gráfico Navi 21 não encontrado no IORegistry.${NC}"
    echo "Aguardando GPU..."
    sleep 2
    continue
  fi

  # 1. Energia e Potência
  POWER=$(echo "$DATA" | grep -o '"Total Power(W)"=[0-9]*' | head -n 1 | cut -d= -f2)
  
  # 2. Clocks e Frequências
  CORE=$(echo "$DATA" | grep -o '"Core Clock(MHz)"=[0-9]*' | head -n 1 | cut -d= -f2)
  MEM=$(echo "$DATA" | grep -o '"Memory Clock(MHz)"=[0-9]*' | head -n 1 | cut -d= -f2)
  
  # 3. Térmico e Ventoinhas
  TEMP=$(echo "$DATA" | grep -o '"Temperature(C)"=[0-9]*' | head -n 1 | cut -d= -f2)
  RPM=$(echo "$DATA" | grep -o '"Fan Speed(RPM)"=[0-9]*' | head -n 1 | cut -d= -f2)
  FAN_PCT=$(echo "$DATA" | grep -o '"Fan Speed(%)"=[0-9]*' | head -n 1 | cut -d= -f2)
  
  # 4. Atividade e Carga
  LOAD=$(echo "$DATA" | grep -o '"GPU Activity(%)"=[0-9]*' | head -n 1 | cut -d= -f2)
  UTIL=$(echo "$DATA" | grep -o '"Device Utilization %"=[0-9]*' | head -n 1 | cut -d= -f2)
  
  # 5. Memória VRAM e GART
  VRAM_USED=$(echo "$DATA" | grep -o '"inUseVidMemoryBytes"=[0-9]*' | head -n 1 | cut -d= -f2)
  VRAM_FREE=$(echo "$DATA" | grep -o '"vramFreeBytes"=[0-9]*' | head -n 1 | cut -d= -f2)
  GART_USED=$(echo "$DATA" | grep -o '"gartUsedBytes"=[0-9]*' | head -n 1 | cut -d= -f2)
  GART_FREE=$(echo "$DATA" | grep -o '"gartFreeBytes"=[0-9]*' | head -n 1 | cut -d= -f2)
  
  # 6. Pipeline 3D e Saúde
  RECOVERY=$(echo "$DATA" | grep -o '"recoveryCount"=[0-9]*' | head -n 1 | cut -d= -f2)
  GFX_SUB=$(echo "$DATA" | grep -o '"HWChannel GFX | Commands Submitted"=[0-9]*' | head -n 1 | cut -d= -f2)
  GFX_CMP=$(echo "$DATA" | grep -o '"HWChannel GFX | Commands Completed"=[0-9]*' | head -n 1 | cut -d= -f2)
  TEX=$(echo "$DATA" | grep -o '"textureCount"=[0-9]*' | head -n 1 | cut -d= -f2)
  CTX_2D=$(echo "$DATA" | grep -o '"context2DCount"=[0-9]*' | head -n 1 | cut -d= -f2)
  CTX_VID=$(echo "$DATA" | grep -o '"contextVideoCount"=[0-9]*' | head -n 1 | cut -d= -f2)

  # Cálculos de VRAM
  VRAM_USED=${VRAM_USED:-0}
  VRAM_FREE=${VRAM_FREE:-0}
  VRAM_TOTAL=$(( VRAM_USED + VRAM_FREE ))
  VRAM_PCT=$(( VRAM_TOTAL > 0 ? (VRAM_USED * 100 / VRAM_TOTAL) : 0 ))
  VRAM_USED_GB=$(awk "BEGIN {printf \"%.2f\", $VRAM_USED / 1073741824}")
  VRAM_TOTAL_GB=$(awk "BEGIN {printf \"%.2f\", $VRAM_TOTAL / 1073741824}")
  VRAM_BAR=$(make_bar $VRAM_PCT 16)

  # Cálculos de GART (Memória compartilhada do sistema)
  GART_USED=${GART_USED:-0}
  GART_FREE=${GART_FREE:-0}
  GART_TOTAL=$(( GART_USED + GART_FREE ))
  GART_PCT=$(( GART_TOTAL > 0 ? (GART_USED * 100 / GART_TOTAL) : 0 ))
  GART_USED_MB=$(( GART_USED / 1048576 ))
  GART_TOTAL_GB=$(awk "BEGIN {printf \"%.2f\", $GART_TOTAL / 1073741824}")
  GART_BAR=$(make_bar $GART_PCT 16)

  # Status de Potência
  PWR_STATUS="${GREEN}[IDLE EFICIENTE / ECONÔMICO]${NC}"
  if [ "${POWER:-0}" -gt 45 ]; then PWR_STATUS="${YELLOW}[CARGA MODERADA]${NC}"; fi
  if [ "${POWER:-0}" -gt 120 ]; then PWR_STATUS="${RED}[ALTA PERFORMANCE / CARGA 3D]${NC}"; fi

  # Status de Core Clock
  CORE_STATUS="${GREEN}[FLOOR 500MHz ATIVO]${NC}"
  if [ "${CORE:-0}" -gt 600 ]; then CORE_STATUS="${CYAN}[BOOST DINÂMICO]${NC}"; fi

  # Status de Ventoinha
  FAN_STATUS="${GREEN}[0dB / MODO SILENCIOSO PASSIVO]${NC}"
  if [ "${RPM:-0}" -gt 0 ]; then FAN_STATUS="${YELLOW}[VENTOINHA ATIVA]${NC}"; fi

  # Barra de carga de GPU
  LOAD_BAR=$(make_bar "${LOAD:-0}" 16)

  # Status de Saúde (Recovery Count)
  HEALTH_STATUS="${GREEN}✓ 0 RESETS (100% ESTÁVEL / SEM TRAVAMENTOS)${NC}"
  if [ "${RECOVERY:-0}" -gt 0 ]; then HEALTH_STATUS="${RED}! ${RECOVERY} RESETS DETECTADOS !${NC}"; fi

  # Verificação de Sensores SMC adicionais (se ferramenta Stats SMC estiver disponível)
  SMC_GPU_TEMP=""
  SMC_CPU_INFO=""
  if [ -x "$SMC_BIN" ]; then
    SMC_RAW_GPU=$($SMC_BIN list -t 2>/dev/null | grep '\[TG0D\]' | awk '{print $2}')
    if [ -n "$SMC_RAW_GPU" ]; then
      SMC_GPU_TEMP=" ${DIM}(SMC TG0D: ${SMC_RAW_GPU}°C)${NC}"
    fi
    SMC_CPU_TEMP=$($SMC_BIN list -t 2>/dev/null | grep '\[TC0D\]' | awk '{print $2}')
    SMC_CPU_PWR=$($SMC_BIN list -p 2>/dev/null | grep '\[PC0C\]' | awk '{print $2}')
    if [ -n "$SMC_CPU_TEMP" ] && [ -n "$SMC_CPU_PWR" ]; then
      CPU_PWR_FMT=$(awk "BEGIN {printf \"%.1f\", $SMC_CPU_PWR}")
      SMC_CPU_INFO="  ${BOLD}💻 CPU Host (Intel Core)${NC} : ${CYAN}${SMC_CPU_TEMP}°C${NC} | Consumo: ${YELLOW}${CPU_PWR_FMT} W${NC}\n"
    fi
  fi

  # Renderização do Dashboard
  clear
  echo -e "${BLUE}========================================================================${NC}"
  echo -e "   ${BOLD}${CYAN}TELEMETRIA AVANÇADA - AMD RADEON RX 6900 XT RED DEVIL (NAVI 21)${NC}"
  echo -e "   ${DIM}Driver: NootRX v1.0.21 | SMU Floor 500MHz | Zero RPM | macOS Sequoia${NC}"
  echo -e "${BLUE}========================================================================${NC}"
  echo -e "  ${BOLD}⚡ Consumo / Potência${NC}   : ${YELLOW}${POWER:-N/A} W${NC}  ${PWR_STATUS}"
  echo -e "  ${BOLD}⏱  Core Clock (GFX)${NC}     : ${GREEN}${CORE:-N/A} MHz${NC}  ${CORE_STATUS}"
  echo -e "  ${BOLD}⏱  Memory Clock (VRAM)${NC}  : ${CYAN}${MEM:-N/A} MHz${NC}  ${DIM}(GDDR6 UCLK)${NC}"
  echo -e "${BLUE}------------------------------------------------------------------------${NC}"
  echo -e "  ${BOLD}🌡  Temperatura (Edge)${NC}   : ${RED}${TEMP:-N/A} °C${NC}${SMC_GPU_TEMP}"
  echo -e "  ${BOLD}🌡  Hotspot & VRAM Temp${NC} : ${DIM}Gerenciamento interno pelo SMU FW (macOS)*${NC}"
  echo -e "  ${BOLD}🌀 Ventoinhas (Fans)${NC}    : ${RPM:-0} RPM (${FAN_PCT:-0}%)  ${FAN_STATUS}"
  echo -e "${BLUE}------------------------------------------------------------------------${NC}"
  echo -e "  ${BOLD}🧠 VRAM Dedicada (16GB)${NC} : [${CYAN}${VRAM_BAR}${NC}] ${VRAM_PCT}% (${VRAM_USED_GB} GB / ${VRAM_TOTAL_GB} GB)"
  echo -e "  ${BOLD}📦 Memória GART (Host)${NC}  : [${BLUE}${GART_BAR}${NC}] ${GART_PCT}% (${GART_USED_MB} MB / ${GART_TOTAL_GB} GB)"
  echo -e "${BLUE}------------------------------------------------------------------------${NC}"
  echo -e "  ${BOLD}⚙  Carga da GPU (Load)${NC}  : [${YELLOW}${LOAD_BAR}${NC}] ${LOAD:-0}% (Dispositivo: ${UTIL:-0}%)"
  echo -e "  ${BOLD}🚀 Comandos Metal (3D)${NC}  : Submetidos: ${CYAN}${GFX_SUB:-0}${NC} | Completos: ${GREEN}${GFX_CMP:-0}${NC}"
  echo -e "  ${BOLD}🎨 Recursos Alocados${NC}   : ${TEX:-0} texturas | 2D: ${CTX_2D:-0} | Vídeo: ${CTX_VID:-0}"
  echo -e "${BLUE}------------------------------------------------------------------------${NC}"
  if [ -n "$SMC_CPU_INFO" ]; then
    echo -en "$SMC_CPU_INFO"
    echo -e "${BLUE}------------------------------------------------------------------------${NC}"
  fi
  echo -e "  ${BOLD}🛡  Saúde do Driver${NC}     : ${HEALTH_STATUS}"
  echo -e "${BLUE}========================================================================${NC}"
  echo -e "${DIM}* Nota Técnica: No macOS, os drivers proprietários da Apple (AMDRadeonX6000)${NC}"
  echo -e "${DIM}  expõem o diodo térmico de encapsulamento (Edge Temp / TG0D). As sondas de${NC}"
  echo -e "${DIM}  Tjunction (Hotspot) e VRAM GDDR6 são reguladas internamente pelo firmware SMU.${NC}"
  echo -e "  Pressione ${BOLD}Ctrl + C${NC} para encerrar o monitor."
  
  sleep 1
done
