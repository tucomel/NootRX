# Guia Completo de Portabilidade e Correção no Linux (amdgpu)
## Para PowerColor AMD Radeon RX 6900 XT Red Devil (Navi 21 XTX)

**PCI ID:** `0x73BF:0xC0` | **Subsystem:** `0x148C:0x2408` (Navi 21 XTX / Sienna Cichlid)  
**Autor:** tucomel  
**Propósito:** Este documento é o guia de contexto definitivo para replicar a solução que desenvolvemos no macOS (Nootrx_rx6900xt_rd v1.0.21) diretamente no driver oficial do Linux (`amdgpu`).

---

## 1. O Problema e a Causa Raiz Descoberta

### O Sintoma
Em sistemas operacionais Unix (macOS e Linux), placas com binning de fábrica extremo como a **PowerColor Red Devil RX 6900 XT** apresentam:
1. **Micro-artefatos horizontais (*tile slicing*)** no desktop, navegador e reprodutores de vídeo durante estados de repouso (*idle*) ou transição de carga 3D para repouso.
2. **Resets e Timeouts de Driver:** O acelerador trava esperando um *stamp* ou comando gráfico (`waitForStamp timeout` no macOS ou `[drm:amdgpu_job_timedout] *ERROR* ring gfx_0.0.0 timeout` no Linux), reiniciando o canal 3D (`Channel 51 GFX` / `ring gfx`).

### A Causa Física Revelada
No Windows, o driver proprietário da AMD mantém:
- **Core Clock:** ~497–501 MHz (piso mínimo fixo).
- **Voltagem do Núcleo:** 0.800 V (800 mV).
- **GFX Status:** **GFX NUNCA ENTRA EM OFF** (os Compute Units nunca são desligados).
- **Consumo:** 18–20W.

No Linux/macOS, o microcódigo da **SMU v11.0.7** aplica por padrão:
- **`FEATURE_GFXOFF_BIT` (Bit 20):** Desliga o núcleo gráfico em repouso. Ao receber uma chamada de renderização repentina, a GPU demora para acordar do GFXOFF, estourando o watchdog do kernel (*ring timeout*).
- **`FEATURE_DS_GFXCLK_BIT` (Bit 12):** Permite *Deep Sleep* do clock gráfico, derrubando a frequência para menos de 100 MHz.
- **`FEATURE_TEMP_DEPENDENT_VMIN_BIT` (Bit 39):** Reduz a voltagem mínima quando a placa está fria (<60°C), derrubando a alimentação abaixo de 0.800V e causando instabilidade de memória nos 80 CUs.
- **`FEATURE_MMHUB_PG_BIT` (Bit 40):** Aplica *Power Gating* (sono) no Hub de Gerenciamento de Memória, gerando atraso nas linhas de varredura (*scanout*).

---

## 2. A Solução Arquitetural Comprovada (Máscara SMU)

No firmware da SMU11 (Sienna Cichlid), a máscara de recursos desativados que garante **100% de estabilidade com consumo equilibrado (40W)** é:

```c
SMU_DisallowedFeatures = 0x38400101004 (Decimal: 3865471619076)
```

### Decomposição dos Bits (SMU11 Feature Bits):
| Bit | Nome do Recurso no Linux Kernel | Valor Hex | Ação no Fix |
| :--- | :--- | :--- | :--- |
| **2** | `FEATURE_DPM_GFX_GPO_BIT` | `0x4` | Desativado por padrão de fábrica |
| **12** | `FEATURE_DS_GFXCLK_BIT` | `0x1000` | **DESATIVADO:** Mantém o piso mínimo do Core Clock em 500 MHz |
| **20** | `FEATURE_GFXOFF_BIT` | `0x100000` | **DESATIVADO:** Impede o desligamento do núcleo (igual Windows) |
| **34** | `FEATURE_GFX_DCS_BIT` | `0x400000000` | **DESATIVADO:** Bloqueia oscilação por *Duty Cycle Scaling* |
| **39** | `FEATURE_TEMP_DEPENDENT_VMIN_BIT` | `0x8000000000` | **DESATIVADO:** Impede queda de Vmin abaixo de 0.800V |
| **40** | `FEATURE_MMHUB_PG_BIT` | `0x10000000000` | **DESATIVADO:** Mantém o Hub de Memória acordado |
| **41** | `FEATURE_ATHUB_PG_BIT` | `0x20000000000` | **DESATIVADO:** Mantém o Hub de Endereçamento acordado |
| **18** | `FEATURE_GFX_ULV_BIT` | `0x40000` | **PERMITIDO (Ativo):** Permite que o clock desça até 500 MHz em 40W |

---

## 3. Arquivos Correspondentes no Código do Kernel Linux

No código-fonte oficial do kernel Linux (pasta `drivers/gpu/drm/amd/`):

1. **Definição dos Bits da SMU:**
   - `drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu11_driver_if_sienna_cichlid.h`
   ```c
   #define FEATURE_DPM_GFXCLK_BIT          1
   #define FEATURE_DPM_GFX_GPO_BIT         2
   #define FEATURE_DS_GFXCLK_BIT           12
   #define FEATURE_GFX_ULV_BIT             18
   #define FEATURE_GFXOFF_BIT              20
   #define FEATURE_GFX_DCS_BIT             34
   #define FEATURE_TEMP_DEPENDENT_VMIN_BIT 39
   #define FEATURE_MMHUB_PG_BIT            40
   #define FEATURE_ATHUB_PG_BIT            41
   ```

2. **Gerenciador de PowerPlay da Sienna Cichlid:**
   - `drivers/gpu/drm/amd/pm/swsmu/smu11/sienna_cichlid_ppt.c`
   - Função de inicialização de recursos: `sienna_cichlid_get_smu_feature_mask()` ou `sienna_cichlid_system_features_control()`.

3. **Display Core Next (DCN 3.0):**
   - `drivers/gpu/drm/amd/display/dc/dcn30/`
   - O equivalente ao `DalDramClockChangeOneDisplayVActive` e controle de VActive DRAM:
     `dram_clock_change_one_display_vactive` no `clk_mgr` de DCN30.

---

## 4. Como Aplicar a Solução no Linux

### Método 1: Parâmetros de Boot no Kernel (`GRUB_CMDLINE_LINUX`)
Sem precisar recompilar o kernel, você pode desativar o GFXOFF e ajustar o comportamento de energia via bootloader:

No `/etc/default/grub`:
```bash
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash amdgpu.ppfeaturemask=0xffffffff amdgpu.runpm=0"
```
*(Depois execute `sudo update-grub`)*.

- `amdgpu.runpm=0`: Desativa o *Runtime Power Management*, impedindo a GPU de entrar em suspensão profunda no barramento PCIe.
- Para desativar o GFXOFF via módulo:
  Crie o arquivo `/etc/modprobe.d/amdgpu.conf`:
  ```ini
  options amdgpu ppfeaturemask=0xfffdffff
  ```

---

### Método 2: Patch Direto no Kernel Linux (Solução Idêntica ao NootRX)
Se estiver compilando um kernel customizado ou módulo DKMS, edite:
`drivers/gpu/drm/amd/pm/swsmu/smu11/sienna_cichlid_ppt.c`

Procure pela função onde as features permitidas são montadas:
```c
static int sienna_cichlid_get_smu_feature_mask(struct smu_context *smu,
					      uint32_t *feature_mask,
					      uint32_t num)
{
	/* ... código original ... */

	/* Fix Red Devil RX 6900 XT (0x73BF:0x148C:0x2408) */
	if (smu->adev->pdev->device == 0x73bf && smu->adev->pdev->subsystem_device == 0x2408) {
		/* Proibir GFXOFF (Bit 20) */
		feature_mask[FEATURE_GFXOFF_BIT / 32] &= ~(1U << (FEATURE_GFXOFF_BIT % 32));
		/* Proibir Deep Sleep do GFXCLK (Bit 12 - trava piso 500MHz) */
		feature_mask[FEATURE_DS_GFXCLK_BIT / 32] &= ~(1U << (FEATURE_DS_GFXCLK_BIT % 32));
		/* Proibir queda de Vmin dependente de temperatura (Bit 39 - trava 0.800V) */
		feature_mask[FEATURE_TEMP_DEPENDENT_VMIN_BIT / 32] &= ~(1U << (FEATURE_TEMP_DEPENDENT_VMIN_BIT % 32));
		/* Proibir Power Gating do Memory Hub (Bit 40) */
		feature_mask[FEATURE_MMHUB_PG_BIT / 32] &= ~(1U << (FEATURE_MMHUB_PG_BIT % 32));
		/* Proibir Power Gating do Address Translation Hub (Bit 41) */
		feature_mask[FEATURE_ATHUB_PG_BIT / 32] &= ~(1U << (FEATURE_ATHUB_PG_BIT % 32));
		/* Proibir GFX Duty Cycle Scaling (Bit 34) */
		feature_mask[FEATURE_GFX_DCS_BIT / 32] &= ~(1U << (FEATURE_GFX_DCS_BIT % 32));
	}

	return 0;
}
```

---

### Método 3: Controle Dinâmico via Sysfs e CoreCtrl / OverDrive
No Linux, com `amdgpu.ppfeaturemask=0xffffffff` habilitado, o sysfs oferece controle total sobre clocks e voltagens:

1. **Travar o Clock Mínimo em 500 MHz:**
   ```bash
   # Definir o nível de performance como manual
   echo "manual" | sudo tee /sys/class/drm/card0/device/power_dpm_force_performance_level

   # Ajustar a curva OD (OverDrive) para piso mínimo de 500 MHz
   echo "s 0 500" | sudo tee /sys/class/drm/card0/device/pp_od_clk_voltage
   echo "c" | sudo tee /sys/class/drm/card0/device/pp_od_clk_voltage
   ```

2. **Desativar DCC no Linux (Mesa / Wayland / X11):**
   Caso ocorra qualquer corrupção em scanout no Linux, o equivalente ao `GPUDCCDisplayable=false` é definir nas variáveis de ambiente globais (`/etc/environment`):
   ```bash
   AMD_DEBUG=nodcc
   RADV_DEBUG=nodcc
   ```

3. **Verificar a Telemetria no Linux:**
   ```bash
   # Visualizar consumo, clocks e temperaturas em tempo real:
   watch -n 1 cat /sys/kernel/debug/dri/0/amdgpu_pm_info
   ```

---

## 5. Resumo das Equivalências macOS (NootRX) vs Linux (amdgpu)

| Recurso NootRX (macOS) | Implementação no Linux (amdgpu) | Efeito Físico |
| :--- | :--- | :--- |
| `rd-nogfxoff` / Bit 20 | `FEATURE_GFXOFF_BIT=0` | Núcleo gráfico nunca desliga; zero timeouts de ring |
| `rd-corefloor` / Bit 12 | `FEATURE_DS_GFXCLK_BIT=0` | Core Clock não desce abaixo de 500 MHz |
| Bit 39 na SMU | `FEATURE_TEMP_DEPENDENT_VMIN_BIT=0` | Voltagem mínima não cai abaixo de 0.800V quando fria |
| Bit 40 na SMU | `FEATURE_MMHUB_PG_BIT=0` | Memory Management Hub 100% acordado; zero tile slices |
| `rd-nodcc` | `AMD_DEBUG=nodcc` | Desativa Delta Color Compression no scanout |
| `DalDramClockChangeOneDisplayVActive=1` | `dram_clock_change_one_display_vactive` | Permite que a memória GDDR6 reduza a frequência |
