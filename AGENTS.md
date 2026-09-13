# NootRX — Memória de Arquitetura e Engenharia: PowerColor RX 6900 XT Red Devil

Este arquivo é a memória técnica, arquitetural e operacional definitiva de todo o desenvolvimento e correção realizados para a **PowerColor AMD Radeon RX 6900 XT Red Devil** no macOS Ventura. Qualquer agente de IA ou desenvolvedor que for manter, evoluir ou portar este projeto DEVE ler este documento na íntegra.

---

## 1. Identificação do Hardware e Ambiente Homologado

- **GPU:** PowerColor AMD Radeon RX 6900 XT Red Devil (Navi 21 XTX).
- **PCI ID:** `0x1002:0x73BF`, Revisão `0xC0`.
- **Subsystem ID:** `0x148C:0x2408`.
- **Plataforma de Teste:** Intel Core i7-6800K (Broadwell-E HEDT), Placa-mãe X99, PCIe 3.0 x16.
- **SMBIOS:** `MacPro7,1`.
- **macOS:** Ventura 13.7.8 (Build `22H730`).
- **Lilu:** 1.7.2.
- **Monitor:** LG Ultrawide 2560x1080 @ 100 Hz conectado via DisplayPort.
- **Kext Produzido:** `Nootrx_rx6900xt_rd.kext` (identificador interno: `org.ChefKiss.NootRX`).

---

## 2. A Causa Raiz Física e a "Arma do Crime"

### O Sintoma Original
A placa apresentava microfragmentos gráficos (pequenos cortes/fatias horizontais de memória desatualizada — *stale horizontal tile slices*) no desktop, barra de tarefas, movimentação de janelas e vídeos no IINA. Em situações críticas de transição de carga (fechamento de jogos/benchmarks 3D), ocorria congelamento de tela e *driver crash*.

### A Descoberta Científica da Causa Raiz
Ao contrário de modelos de referência da AMD ou módulos Apple MPX (Radeon Pro W6800X), a **Red Devil** possui um silício de binning agressivo com 80 Compute Units (CUs) e overclock de fábrica extremo.

1. **A Telemetria do Windows (`docs/diagnostics/windows_baseline/2min desktop.txt`):**
   - No Windows, o driver oficial da AMD mantém o Core Clock em **497–501 MHz** fixo e a voltagem em **0.800V (800 mV)** em repouso.
   - O mais importante: **o GFX NÃO DESLIGA no Windows** (GFXOFF permanece inativo).
   - Consumo em idle no Windows: 18–20W com 0% artefatos.

2. **O Que o macOS Fazia por Padrão:**
   - O driver da Apple (`AMDRadeonX6000`) ativava todos os modos de sono agressivo da SMU (`SMU_DisallowedFeatures = 4`).
   - Isso fazia o clock despencar para <100 MHz, a voltagem cair abaixo de 0.800V para Ultra Low Voltage (ULV), o núcleo desligar (*GFXOFF*) e o Hub de Memória (*MMHUB*) dormir.
   - Quando o WindowServer enviava comandos de desenho 2D parciais (*dirty rects*), a memória e o núcleo demoravam microssegundos a mais para responder, resultando na retenção visual de pixels antigos (os micro-artefatos).

3. **A Prova Definitiva no Teste da v1.0.19 (`docs/diagnostics/macos_v1.0.19_crash/`):**
   - Quando removemos o bloqueio de GFXOFF e ULV na v1.0.19, o kernel capturou a falha exata:
     ```text
     kernel: (IOAcceleratorFamily2) virtual IOReturn IOAccelEventMachine2::waitForStamp: timeout waiting for AMDRadeonAccelerator stamp 78 (gpu_stamp=77)
     kernel: (AMDRadeonX6000) GPU Log Version: 2
     Restart Channel: 51 GFX
     ```
   - O núcleo entrou em GFXOFF, não acordou a tempo do watchdog do macOS e derrubou o acelerador gráfico com chuva de artefatos.

---

## 3. A Solução Arquitetural Definitiva: Versão v1.0.21 (Golden Release)

A versão **v1.0.21** estabeleceu o equilíbrio perfeito entre **estabilidade 100% absoluta (zero artefatos e zero resets)** e **baixo consumo térmico (40W–42W em idle)**.

### A. A Máscara de Firmware da SMU (`SMU_DisallowedFeatures`)
No arquivo `NootRX/NootRX.cpp`, a máscara injetada em `aty_properties` para a GPU é:
```c
SMU_DisallowedFeatures = 0x38400101004 (Decimal: 3865471619076)
```

Bits desativados na SMU11 (Sienna Cichlid):
- **Bit 12 (`FEATURE_DS_GFXCLK_BIT` - `0x1000`):** Proíbe o *Deep Sleep* do clock gráfico, travando o piso mínimo em **500 MHz**.
- **Bit 20 (`FEATURE_GFXOFF_BIT` - `0x100000`):** **Proíbe o desligamento do núcleo (GFXOFF)**, mantendo os CUs sempre alimentados exatamente como no Windows.
- **Bit 34 (`FEATURE_GFX_DCS_BIT` - `0x400000000`):** Proíbe oscilação artificial de frequência por *Duty Cycle Scaling*.
- **Bit 39 (`FEATURE_TEMP_DEPENDENT_VMIN_BIT` - `0x8000000000`):** Proíbe a redução da voltagem mínima quando a placa está fria, **mantendo a alimentação em ~0.800V**.
- **Bit 40 (`FEATURE_MMHUB_PG_BIT` - `0x10000000000`):** Proíbe o sono (*Power Gating*) do Hub de Memória, garantindo taxas instantâneas de resposta de frame.
- **Bit 41 (`FEATURE_ATHUB_PG_BIT` - `0x20000000000`):** Proíbe o sono do Hub de Endereçamento de Tradução.
- **Bit 18 (`FEATURE_GFX_ULV_BIT`):** **PERMITIDO (Ativo):** Ao permitir o ULV na SMU, o firmware não é obrigado a pular para o estado Boost DPM (2524 MHz / 64W da v1.0.18), permitindo que a GPU assente suavemente em **500–600 MHz** com consumo de apenas **40W–42W**.

### B. Liberação Dinâmica da GDDR6 no Monitor Único DisplayPort
Quando a flag `rd-novactivedram` está desativada (`0`), o driver injeta em `aty_properties`:
```xml
<key>DalDramClockChangeOneDisplayVActive</key>
<integer>1</integer>
```
Isso autoriza a controladora DCN a alternar dinamicamente a frequência das memórias GDDR6 no monitor único DisplayPort sem piscar a tela, permitindo que a memória colabore para a redução do consumo térmico.

---

## 4. Tabela Completa de Flags em DeviceProperties

As flags são configuradas no nó PCI da GPU no OpenCore (`config.plist`):

| Flag | Valor Recomendado | Ação no Driver |
| :--- | :--- | :--- |
| `rd-corefloor` | `<data>MDE=</data>` (`01`) | Aplica a máscara SMU calibrada `0x38400101004` |
| `rd-nogfxoff` | `<data>AQ==</data>` (`01`) | Injeta `PP_GfxOffControl=0` |
| `rd-noulv` | `<data>AQ==</data>` (`01`) | Injeta `PP_DisableULV=1` no driver |
| `rd-nompo` | `<data>AQ==</data>` (`01`) | Injeta `DalForceSingleDispPipeSplit=1` (evita bugs de MPO) |
| `rd-nostutter` | `<data>AQ==</data>` (`01`) | Desativa stutter clocks (estabilidade) |
| `rd-nodcc` | `<data>AQ==</data>` (`01`) | Força `GPUDCCDisplayable=false` (evita corrupção de scanout DCC) |
| `rd-floordpm` | `<data>AA==</data>` (`00`) | **00:** Permite que os clocks desçam dinamicamente até o piso |
| `rd-novactivedram` | `<data>AA==</data>` (`00`) | **00:** Habilita `DalDramClockChangeOneDisplayVActive=1` |
| `rd-diag` | `<data>AQ==</data>` (`01`) | Habilita logs completos no boot para diagnóstico |

---

## 5. Resultados de Validação em Teste de Estresse (v1.0.21)

Arquivo de auditoria: `docs/diagnostics/macos_v1.0.21_definitive/ioreg_accelerator.txt`
- **Tempo de Teste:** >30 minutos contínuos com Unigine Heaven pesado + IINA Player reproduzindo simultaneamente, seguido de parada do Heaven.
- **Comandos Submetidos:** **678.210 comandos gráficos** concluídos com sucesso (`fSubmissionsSinceLastCheck`).
- **Resets de Driver:** **`recoveryCount = 0`** (zero resets!).
- **Timeouts:** **Zero ocorrências de timeout** no kernel log.
- **Consumo em Carga Mista (IINA ativo):** **44 Watts**.
- **Consumo em Repouso Puro (Idle):** **40 W – 42 Watts** (contra 64W da v1.0.18).
- **Temperatura:** **59 °C** sob estresse, caindo para **~50 °C** em repouso com ventoinhas em **0 RPM** (modo passivo silencioso).
- **Artefatos:** **0.00% (Zero absoluto).**

---

## 6. Recursos de Diagnóstico no Repositório

- `docs/diagnostics/macos_v1.0.21_definitive/`: Logs completos do teste de estresse aprovado.
- `docs/diagnostics/macos_v1.0.19_crash/`: Logs de auditoria do crash por GFXOFF.
- `docs/diagnostics/windows_baseline/`: Telemetria de referência do Windows e dump de VBIOS.
- `docs/opencore/sample_deviceproperties.plist`: Plist pronto para copiar e colar no OpenCore.
- `scripts/monitor_gpu.sh`: Monitor de telemetria em tempo real para a RX 6900 XT.
- `scripts/monitor_rx5700xt.sh`: Monitor de telemetria em tempo real para a RX 5700 XT.
- `LINUX_PORTING_GUIDE.md`: Guia de transposição da solução para o driver `amdgpu` no Linux.
