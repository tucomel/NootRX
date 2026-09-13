#!/bin/bash
# ==============================================================================
# Monitor de Telemetria em Tempo Real - AMD Radeon RX 6900 XT Red Devil
# ==============================================================================
# Uso: /Volumes/EFI/monitor_gpu.sh  (ou arraste para o Terminal)
# Para sair: Ctrl + C
# ==============================================================================

# Cores
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
RED='\033[0;31m'
BOLD='\033[1m'
NC='\033[0m'

# Captura Ctrl+C para sair limpando a tela
trap 'echo -e "\n${GREEN}Monitoramento encerrado.${NC}"; exit 0' SIGINT SIGTERM

while true; do
  DATA=$(ioreg -l -w0 -r -c AMDRadeonX6000_AMDNavi21GraphicsAccelerator 2>/dev/null)
  
  if [ -z "$DATA" ]; then
    clear
    echo -e "${RED}Erro: Acelerador gráfico Navi 21 não encontrado.${NC}"
    echo "Aguardando GPU..."
    sleep 2
    continue
  fi

  POWER=$(echo "$DATA" | grep -o '"Total Power(W)"=[0-9]*' | head -n 1 | cut -d= -f2)
  CORE=$(echo "$DATA" | grep -o '"Core Clock(MHz)"=[0-9]*' | head -n 1 | cut -d= -f2)
  MEM=$(echo "$DATA" | grep -o '"Memory Clock(MHz)"=[0-9]*' | head -n 1 | cut -d= -f2)
  TEMP=$(echo "$DATA" | grep -o '"Temperature(C)"=[0-9]*' | head -n 1 | cut -d= -f2)
  RPM=$(echo "$DATA" | grep -o '"Fan Speed(RPM)"=[0-9]*' | head -n 1 | cut -d= -f2)
  FAN_PCT=$(echo "$DATA" | grep -o '"Fan Speed(%)"=[0-9]*' | head -n 1 | cut -d= -f2)
  LOAD=$(echo "$DATA" | grep -o '"GPU Activity(%)"=[0-9]*' | head -n 1 | cut -d= -f2)

  clear
  echo -e "${BLUE}======================================================${NC}"
  echo -e "${BOLD}${CYAN}   TELEMETRIA AO VIVO - AMD RADEON RX 6900 XT         ${NC}"
  echo -e "${BLUE}======================================================${NC}"
  printf "  ${BOLD}%-22s${NC} : ${YELLOW}%s W${NC}\n" "Consumo (Potência)" "${POWER:-N/A}"
  printf "  ${BOLD}%-22s${NC} : ${GREEN}%s MHz${NC}\n" "Core Clock (GPU)" "${CORE:-N/A}"
  printf "  ${BOLD}%-22s${NC} : ${CYAN}%s MHz${NC}\n" "Memory Clock (VRAM)" "${MEM:-N/A}"
  printf "  ${BOLD}%-22s${NC} : ${RED}%s °C${NC}\n" "Temperatura" "${TEMP:-N/A}"
  printf "  ${BOLD}%-22s${NC} : %s RPM (%s%%)\n" "Ventoinha" "${RPM:-0}" "${FAN_PCT:-0}"
  printf "  ${BOLD}%-22s${NC} : %s%%\n" "Uso da GPU" "${LOAD:-0}"
  echo -e "${BLUE}======================================================${NC}"
  echo -e "  Pressione ${BOLD}Ctrl + C${NC} para encerrar o monitor."
  
  sleep 1
done
