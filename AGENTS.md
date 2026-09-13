# NootRX Red Devil RX 6900 XT — estado da investigação

Este arquivo é a memória operacional e técnica do trabalho realizado em
2026-09-12/13. Leia-o por completo antes de alterar, compilar ou instalar este
kext. O histórico cronológico detalhado também está no grande comentário de
decisões em `NootRX/NootRX.cpp`, próximo de `NootRXMain::wrapAddDrivers`.

O documento `docs/research-20260913-rx6900xt-red-devil-artifacts.md` preserva
pesquisa e fontes úteis, mas sua seção “Plano de validação” é histórica: ela
ainda recomenda AGDP e 24-bit como próximos testes, ambos já executados depois.
Para trabalho futuro, use este arquivo e
`docs/plan-20260913-v103-to-zero-artifacts.md` como estado mais recente.

## Objetivo e regra principal

O objetivo é eliminar artefatos de interface e resets do display em uma
PowerColor Red Devil RX 6900 XT no macOS Ventura sem sacrificar aceleração
Metal/OpenGL, estabilidade ou treinamento GDDR6.

A versão experimental chamada `1.0.3` é a única baseline aprovada. Ela resolve
aproximadamente 99,5% dos artefatos e foi a configuração mais estável medida,
mas ainda não é uma solução de 100%. Nunca prometa 100% sem um teste real no
PC, incluindo idle, vídeo, carga 3D e a transição carga -> idle.

Antes de qualquer novo experimento:

1. Preserve as tags `1.0.3` e `v1.0.3-nodcc-fix` sem mover ou apagar.
2. Confirme que ambas resolvem para o commit
   `429c6ab3243b8b51d3a674773aafd11a5558f066`.
3. Mude somente uma variável por versão.
4. Nunca combine flags ou patches que já falharam.
5. Documente no código a hipótese, mecanismo, limites e resultado observado.
6. Faça backup local antes de escrever na EFI.
7. Não use a EFI como arquivo histórico; nela ficam somente dados necessários
   ao release/teste atual.

## Hardware e ambiente confirmados

- GPU: PowerColor Red Devil Radeon RX 6900 XT.
- PCI ID: `1002:73BF`, revisão `C0`.
- Subsystem: `148C:2408`.
- Plataforma: X99, Intel Core i7-6800K, PCIe 3.0 x16.
- SMBIOS observado: `MacPro7,1`.
- macOS: Ventura 13.7.8, build `22H730`.
- Lilu usado nos testes: 1.7.2.
- Driver carregado: `org.ChefKiss.NootRX`.
- O monitor/link foi testado a 100 Hz. Capturas diferentes exibiram resoluções
  lógicas/ativas diferentes; não assuma uma única resolução como causa.
- A mesma placa, plataforma e monitor funcionam normalmente no Windows.

## Sintoma reproduzível

- Pequenos tiles/quadrados coloridos ou pretos aparecem no desktop, barra de
  menus, animações, janelas e vídeo no IINA.
- Um tile persistente desaparece ao redesenhar a região com o mouse.
- Captura/gravação de tela reduziu ou eliminou visualmente o problema em testes
  anteriores, sugerindo diferença no caminho de composição/scanout.
- Heaven sob carga remove praticamente todos os fragmentos. Eles reaparecem
  imediatamente ou logo depois que Heaven é fechado.
- Em configurações ruins, após a transição de carga para idle ocorreram tela
  amarela, congelamento breve, `IOAccelDisplayPipe` timeout e restart do canal
  GFX/driver.
- O comportamento aponta para o caminho de display/composição em baixa carga,
  não para falta de desempenho 3D.
- Windows tolera as mesmas mudanças agressivas de clock e o mesmo hardware sem
  corrupção. Portanto, um clock baixo isolado não prova a causa.

## Baseline imutável 1.0.3

Tags e commit:

- `1.0.3`
- `v1.0.3-nodcc-fix`
- commit `429c6ab3243b8b51d3a674773aafd11a5558f066`

Binário aprovado:

- caminho de backup:
  `/Users/arthur/GIT/rx6900xt/backups/efi/20260913-0438-before-v1.0.11-force8bpc/NootRX_fix.kext`
- executável SHA-256:
  `fc17f9d4bb9b01a2a8ffb6cd81e967538a4a81694f3b09582d1d58e429ff4629`
- executável MD5: `a7ed288083b54817460828db7cc58c4b`
- o `Info.plist` desse binário ainda anuncia versão interna `1.0.0`; isso é
  esperado. Identifique a baseline pelos hashes e pela tag, não apenas pelo
  `CFBundleVersion`.

Config aprovada:

- caminho de backup:
  `/Users/arthur/GIT/rx6900xt/backups/efi/20260913-0438-before-v1.0.11-force8bpc/config.plist`
- SHA-256:
  `f69b1cdfd2e5c9b4827c458ed8ff93d79c5bc246aeee7feda3daeffd3557921c`
- propriedades ativas no nó PCI da GPU:
  `rd-diag`, `rd-floordpm`, `rd-nodcc`, `rd-nogfxoff`, `rd-nompo`,
  `rd-nostutter`, `rd-noulv` e `rd-novactivedram`, todas com byte `01`.
- `rd-no2step` deve estar ausente.
- `rd-force8bpc` deve estar ausente.
- `NootRX.kext` upstream permanece desativado e `NootRX_fix.kext` permanece
  ativado na lista `Kernel/Add`.

Efeitos importantes preservados pela baseline:

- `GPUDCCDisplayable=false` no acelerador Navi 21/Navi 23.
- `PP_GfxOffControl=0`.
- `PP_DisableULV=1`.
- `DalDisableVActiveDramChange=1`.
- `DalForceSingleDispPipeSplit=1`.
- clocks de stutter desativados pela política já documentada no código.
- `DalForceMinDpmLevel=3` pela configuração atual.
- treinamento GDDR6 nativo e DPM de memória preservados.

Resultados da baseline:

- Primeiro teste relevante: cerca de 99,5% dos artefatos removidos.
- Geekbench Metal chegou a aproximadamente 207K na posição OC.
- Heaven foi observado em aproximadamente 140 fps sem perda aparente de 3D.
- Na posição física SILENT, Geekbench Metal ficou em aproximadamente 198K e os
  fragmentos residuais diminuíram ainda mais ao longo de cerca de dez minutos.
- A captura SILENT mais longa chegou a cerca de 717 segundos de uptime sem
  timeout de display, restart, VM fault, hang ou falha de treinamento GDDR6.
- SILENT + 1.0.3 é a melhor combinação observada, mas ainda houve fragmentos
  residuais; não a declare como solução total.

### Mapa cronológico de tags/commits locais

| Marco | Commit | Interpretação |
| --- | --- | --- |
| `baseline-solid`, `v1.0.0-baseline-solid` | `5fcea54` | Primeira baseline que mascarou cerca de 99% do problema. |
| `v1.0.1-pipe-split-fix` | `0a3eb08` | Experimento histórico de política de pipe split; foi superado pela configuração posterior. |
| `v1.0.2-mclk-lock` | `b8f2559` | Rejeitado por panic no treinamento GDDR6. |
| `1.0.3`, `v1.0.3-nodcc-fix` | `429c6ab` | Baseline aprovada e imutável. |
| `experiment-vactive-failed` | `b525d8e` | VActive nativo rejeitado por regressão. |
| `v1.0.4-pipesplit-avoid-test` | `99c144b` | Rejeitado por tela amarela/resets. |
| `v1.0.5-single-channel-test` | `760abd2` | Sem melhora; rejeitado. |
| `v1.0.6-native-passthrough-test` | `0291553` | Guard/passthrough não forneceu controle nativo válido. |
| `v1.0.7-native-passthrough-test` | `8750335` | Regressão catastrófica; rejeitado. |
| `v1.0.8-agdp-macpro71-test` | `0a3a38d` | Mais fragmentos e reset; rejeitado. |
| `v1.0.9-force24-test` | `5c7998e` | Teste inválido porque o kext alvo não carregou. |
| `experiment-native-weg-control-20260913` | `5b30852` | Controle WEG limpo reproduziu artefatos e foi pior que 1.0.3. |
| `v1.0.10-no2step-test` | `1c5d21f` | Bit 46 efetivamente aplicado, mas causou regressão/reset. |
| `experiment-no2step-failed` | `6c67399` | Registro definitivo da rejeição da 1.0.10. |
| `observation-baseline103-silent` | `e936e89` | SILENT + 1.0.3 tornou-se a melhor candidata observada. |
| `observation-baseline103-silent-10min` | `4e3ad02` | Observação estendida sem reset; ainda com resíduo. |
| `research-windows-vbios-reference` | `0ef1c5f` | VBIOS/telemetria Windows registrados. |
| `v1.0.11-force8bpc-test` | `7b1446a` | Remapeamento coerente de IOFramebuffer, mas não do link DAL. |
| `experiment-force8bpc-ineffective-slow-failed` | `94ad3f4` | Registro definitivo da rejeição da 1.0.11. |
| `v1.0.12-cfgnodcc-test` | `d742456` | Variável única: CFG_NO_DCC=true no aty_config para desativar DCC no controlador DCN (consumidor). |
| `experiment-cfgnodcc-failed` | `d742456` | Rejeitado conclusivamente por aumento severo de fragmentos no desktop e corrupção persistente. |
| `v1.0.13-noidlepower-test` | `fca3c93` | Variável única: DalDisableIdlePowerOptimizations=1 para impedir power gating de front-ends DCN e timeouts do MPCC. |
| `experiment-noidlepower-failed` | `fca3c93` | Rejeitado conclusivamente por persistência de artefatos sob Heaven, piora após saída e crash do driver de vídeo ao encerrar gravação de tela (DisplayPipe stamp 57 timeout / Restart Channel GFX). |
| `v1.0.14-floordpm4-test` | `c147899` | Variável única: DalForceMinDpmLevel=4 (1000 MHz / DPM Max floor) para eliminar oscilação 673-1000 MHz em idle. |
| `experiment-floordpm4-failed` | `c147899` | Rejeitado conclusivamente: memória travou em 1000 MHz (1990 MHz efetivos), mas fragmentos permaneceram iguais; colisão de stamp entre screencapture e Heaven (timeout stamp 5643 / Restart Channel 4 ComputeUQ1). Prova que UCLK não é a causa raiz. |
| `v1.0.15-corefloor500-test` | `8cd9cfb` | Variável única: PP_GfxclkDeepSleepDisable=1 e PP_SclkDeepSleepDisable=1 (tentativa de piso 500 MHz) com memória livre (sem DalForceMinDpmLevel). |
| `experiment-corefloor500-failed` | `8cd9cfb` | Rejeitado conclusivamente: chaves PP_*DeepSleepDisable inexistentes no driver da Apple; remoção de DalForceMinDpmLevel derrubou Core Clock para 56-62 MHz em idle, piorando fragmentos, causando timeout DisplayPipe stamp 57, 2 resets de driver (GFX e ComputeUQ3) e abortando gravação de tela. Heaven eliminou 100% dos fragmentos enquanto rodava, voltando ao fechar. |
| `v1.0.16-core500-smu-test` | `f46e153` | Variável única: SMU_DisallowedFeatures com Bit 12 (DS_GFXCLK) e Bit 34 (GFX_DCS) desativados (0x400001004) para travar piso do Core Clock em 500 MHz via firmware da SMU, preservando DalForceMinDpmLevel=3 intacto. |

## Experimentos rejeitados — não repetir nem combinar

### PP_GfxclkDeepSleepDisable / Memória Livre (v1.0.15)

- `v1.0.15-corefloor500-test`: Injetar `PP_GfxclkDeepSleepDisable=1` e `PP_SclkDeepSleepDisable=1` e remover `DalForceMinDpmLevel` revelou que essas chaves de deep sleep não existem no driver macOS (`AMDRadeonX6000`). Sem `DalForceMinDpmLevel=3`, o DPM despencou para o piso absoluto (Nível 0), fazendo o Core Clock cair para **56–62 MHz** (pior que os 126–150 MHz da baseline 1.0.3).
- Consequências: piora imediata dos fragmentos no desktop, timeout do DisplayPipe aos 50s (`stamp index 57 time out`), dois resets de driver (`Restart Channel: 51 GFX` e `Restart Channel: 6 ComputeUQ3`), e timeout de sincronização entre Heaven e captura de tela (`waitForStamp timeout stamp 742`).
- **Achado Crítico Crucial:** Durante a execução do Heaven, com o Core Clock elevado para >1500 MHz, **100% dos fragmentos sumiram completamente (0.00% artefatos)**. Ao fechar o Heaven e o clock retornar a 56 MHz, os fragmentos voltaram imediatamente. Isso confirma 100% que os fragmentos residuais são consequência direta da queda do clock do núcleo em repouso 2D.
- Conclusão: `DalForceMinDpmLevel=3` NUNCA deve ser removido (é o único piso que impede queda para DPM 0). Rollback para a baseline 1.0.3.

### DalForceMinDpmLevel=4 (v1.0.14)

- `v1.0.14-floordpm4-test`: Injetar `DalForceMinDpmLevel=4` travou o clock de memória em 1000 MHz real (1990 MHz efetivo) no macOS conforme confirmado pelo IORegistry. No entanto, os fragmentos residuais de 0.5% continuaram inalterados. Ao iniciar o Heaven enquanto gravava a tela, o driver congelou brevemente e abortou a gravação com timeout de sincronização (`waitForStamp timeout stamp 5643`, `Restart Channel: 4 ComputeUQ1`).
- Conclusão: O clock de memória (UCLK) NÃO é o culpado pelos fragmentos (no Windows a memória roda a 8-14 MHz em idle com zero fragmentos). Travar a memória no teto aumenta consumo (40W) e temperatura (61°C) sem resolver o problema. Rollback para DalForceMinDpmLevel=3.

### DalDisableIdlePowerOptimizations (v1.0.13)

- `v1.0.13-noidlepower-test`: Injetar `DalDisableIdlePowerOptimizations=1` via `rd-noidlepower` causou regressão severa: artefatos persistiram mesmo durante a execução do Heaven, pioraram ao fechar o Heaven, e ao encerrar a gravação de tela o macOS sofreu crash completo no driver de vídeo (`IOAccelDisplayPipe (fbindex=0) transaction stamp index 57 time out`, `Restart Channel: 51 GFX`), corrompendo a gravação. O log confirmou timeout em `mpc2_assert_idle_mpcc line:484` e `DRAM_CLK_CHANGE_WATERMARK_A = 0`.
- Conclusão: As otimizações de idle power do DCN não podem ser desligadas arbitrariamente; desligá-las quebra o estado da máquina de transição do MPCC e causa travamento do pipeline de display. Nunca reative `DalDisableIdlePowerOptimizations=1`.

### DCC no Framebuffer (CFG_NO_DCC)

- `v1.0.12-cfgnodcc-test`: Injetar `CFG_NO_DCC=true` no `aty_config` para desativar o decodificador DCC no controlador de display causou regressão severa: fragmentos por toda a interface, pior que a baseline 1.0.3, e fragmentos estáticos persistiram mesmo durante o Heaven até o cursor do mouse redesenhar por cima.
- Conclusão: O controlador de display (DCN) PRECISA manter a decodificação DCC ativa para os buffers gerados pela GPU; desativar no controlador quebra o layout de tiles. Nunca ative `CFG_NO_DCC=true`. Rollback imediato para 1.0.3.

### MCLK/PowerPlay

- `v1.0.2-mclk-lock`: `PP_MclkDpmDisabled=1` junto de
  `CFG_FORCEMAXDPM=true` causou kernel panic em
  `doGddr6LongTrainingEv` com `GDDR6 Long Training Failed`.
- Regra permanente: nunca desative MCLK DPM nem force DPM máximo antes do
  treinamento. O cold boot da Navi 21 precisa treinar a GDDR6 nativamente.
- Não invente outro clamp de UCLK apenas porque Heaven mascara o sintoma.

### VActive, pipe split e canais

- Restaurar mudanças nativas de VActive DRAM causou timeouts e resets.
- `v1.0.4-pipesplit-avoid-test`: evitar pipe split causou telas amarelas e
  resets; `DalForceSingleDispPipeSplit=1` deve permanecer como na baseline.
- `v1.0.5-single-channel-test`: restaurar `GPUTaskSingleChannel` isoladamente
  não mudou artefatos nem a assinatura do reset.

### Passthrough e AGDP

- `v1.0.6-native-passthrough-test` não produziu um controle nativo válido.
- `v1.0.7-native-passthrough-test` foi dramaticamente pior. NootRX ainda estava
  carregado, substituindo catálogo/propriedades e patchando AGDP; portanto não
  trate esse boot como controle Apple puro.
- `v1.0.8-agdp-macpro71-test`: restaurar a exceção AGDP MacPro7,1 aumentou os
  fragmentos e houve reset GFX.
- Controle nativo limpo com Lilu 1.7.2 + WhateverGreen 1.7.0 +
  `agdpmod=pikera`, ambos os NootRX desativados e todas as propriedades `rd-*`
  removidas: reproduziu os mesmos artefatos e foi pior que a 1.0.3.
- Conclusão: NootRX não criou a causa original; a política da 1.0.3 mascara a
  maior parte de um problema que também existe no stack nativo do Ventura.

### FEATURE_2_STEP_PSTATE

- `v1.0.10-no2step-test` adicionou somente o bit 46 de
  `SMU_DisallowedFeatures`, identificado como `FEATURE_2_STEP_PSTATE_BIT`.
- O teste foi válido: log mostrou `rd-no2step=1` e IORegistry mostrou
  `SMU_DisallowedFeatures=0x400000000004`.
- Antes do primeiro fragmento visível houve timeout do DisplayPipe em cerca de
  50,6 segundos, tela amarela e restart do canal GFX.
- Resultado: pior que a 1.0.3. Nunca ative `rd-no2step` novamente.

### Tentativas de 24-bit/8-bpc

- `v1.0.9-force24-test` não testou 24-bit de verdade. Ele seguiu o estilo
  `-rad24` do WhateverGreen, mas mirou `AMDFramebuffer.kext`, que não carrega no
  caminho Navi 21. Não apareceu marcador e o framebuffer permaneceu
  ARGB2101010/30-bit.
- O `-rad24` oficial do WhateverGreen força/patcha o framebuffer Radeon legado;
  ele não implementa diretamente o caso Navi 21 em
  `AMDRadeonX6000Framebuffer`.
- `v1.0.11-force8bpc-test` roteou em conjunto
  `AmdRadeonFramebuffer::setDisplayMode(int,int)` e
  `getPixelInformation(int,int,int,IOPixelInformation*)`, remapeando índice de
  profundidade 2 para 1.
- O primeiro relato foi “tela preta após verbose”; correção posterior: o boot
  ficou preto por bastante tempo, mas finalmente chegou à tela de login.
- O marcador das duas rotas apareceu. Não apareceu no log o marcador de
  remapeamento em `setDisplayMode`, indicando que a chamada observada não chegou
  ao wrapper com depth 2.
- `system_profiler` passou a anunciar `24-Bit Color (ARGB8888)`.
- Apesar disso, DAL continuou fazendo commit com `displaycolorDepth:2` e AGDP
  continuou validando/modeset com `pBPC=2`. O link real permaneceu em 10-bpc.
- Os fragmentos ficaram iguais. Não houve reset de display/driver, nem depois
  de fechar Heaven, no diagnóstico capturado.
- Resultado: rejeitado por falta de eficácia e demora no vídeo/login. Não use
  `rd-force8bpc` e não teste um dos wrappers isoladamente.
- Tags históricas:
  `v1.0.11-force8bpc-test`, `experiment-force8bpc-black-screen-failed` e
  `experiment-force8bpc-ineffective-slow-failed`.
- A implementação falhou, mas o teste não excluiu logicamente uma futura
  mudança 8-bpc no local correto. O proprietário real da profundidade do link
  está em outro caminho DAL/AGDP/timing, não no registro público de
  `IOPixelInformation`.

## Diagnósticos que não devem virar “causa” isoladamente

- `mpc2_assert_idle_mpcc` apareceu duas vezes tanto em boots utilizáveis quanto
  em testes. Sozinho não prova o reset.
- `DRAM_CLK_CHANGE_WATERMARK_A calculated =0` também ocorreu em boot estável;
  não patchar watermarks apenas por essa linha.
- Mensagens AGDP de sequência inválida foram seguidas por validações aceitas em
  boots estáveis. Não ataque essas mensagens sem correlação com timeout/reset.
- O diagnóstico da v1.0.11 teve zero `IOAccelDisplayPipe timeout`, zero restart
  de canal/GPU, zero `VM_FAULT` e zero falha de treinamento GDDR6. Os artefatos
  podem existir sem reset.

## Referência Windows e VBIOS

Captura local:

`/Users/arthur/GIT/rx6900xt/backups/windows/20260913-0358-reddevil-oc`

Fatos confirmados:

- O operador confirmou que a captura foi feita com a chave física em OC; não
  houve captura Windows na posição SILENT.
- ROM válida de 1 MiB.
- Identificador: `113-D41201-XT`.
- versão VBIOS: `020.001.000.047.000000`.
- SHA-256 da ROM:
  `49d55277ff79f63e5a918857f26eb748206a2e47d7c211b8dcd94d75229aaa91`.
- PowerPlay v15 da ROM: GFXCLK aproximadamente 500..2660 MHz; estados UCLK
  97/457/674/1000 MHz. Esses níveis coincidem com o que o SMU enumera no macOS.
- A ROM contém política 281 W/2340 MHz, mas GPU-Z mostrou defaults 2015/2250
  MHz, números publicados para SILENT. Preserve essa discrepância sem contrariar
  a confirmação física OC e sem inferir qual política o software aplicou.
- Nunca faça flash da ROM como parte desta investigação.
- No Windows, idle ficou perto de 497..502 MHz de GFX, memória reportada variou
  aproximadamente 8..846 MHz a 0,800 V e cerca de 15 W.
- Heaven usou aproximadamente 2,4..2,5 GHz de GFX e 1988..1994 MHz de memória.
- Ao fechar Heaven, houve amostra de memória caindo para cerca de 20 MHz sem
  corrupção. Vídeo depois manteve memória próxima de 1988..1990 MHz com GFX
  perto de 502 MHz.
- Isso prova que a placa física tolera transições e estados baixos no Windows;
  substituir PowerPlay ou travar UCLK não é justificado pela evidência.
- A diferença mais clara observada foi Windows em SDR RGB 8-bpc versus Ventura
  selecionando link 10-bpc (`pBPC=2`). A tentativa v1.0.11 mostrou que mudar só
  a superfície do IOFramebuffer não muda esse link.

## Engenharia reversa do Ventura 22H730

### Kernel Collection

- Os bundles AMD em `/System/Library/Extensions` são stubs de metadados no
  Ventura 22H730; em geral não possuem `Contents/MacOS`.
- O código carregado está em
  `/System/Library/KernelCollections/SystemKernelExtensions.kc`.
- Cópia preservada:
  `/Users/arthur/GIT/rx6900xt/backups/ventura/20260913-0423-22H730-kernel-collection/SystemKernelExtensions.kc`
- tamanho: 373.374.976 bytes.
- SHA-256:
  `865ee134f3004a2a1d02b3eb34df66fef502b9e0e93c80b65a8e26547554e234`.
- O `nm` da versão atual do macOS não conseguiu ler diretamente esse KC por
  incompatibilidade de formato/relocations; isso não significa coleção
  corrompida.
- A extração foi feita com a ferramenta oficial Blacktop `ipsw` v3.1.718.

### AMDRadeonX6000Framebuffer exato

- Bundle carregado reporta 4.1.4; `LC_SOURCE_VERSION` observado é 4.14.4.
- UUID: `E116BA99-722F-3D23-BC50-43C9564FADB7`.
- binário extraído:
  `/Users/arthur/GIT/rx6900xt/backups/ventura/20260913-0423-22H730-kernel-collection/extracted/com.apple.kext.AMDRadeonX6000Framebuffer`
- SHA-256:
  `59fd59bee5c6e6ad1e7b7d2918f9b5851262c0301268244cfae7837adb7c9407`.
- fileset começa em VM/file `0xC66A000` / `208052224`.
- `__TEXT,__text`: VM `0xC66B190`, tamanho 2.022.697.
- `__TEXT,__const`: VM `0xC891960`.
- `__DATA,__data`: VM `0xC923C40`.
- `getPixelInformation` fica em `0xC6B229E`.
- `setDisplayMode(int,int)` fica em `0xC693A60`.
- `updateResourceConfiguration` fica em `0xC6937C6`.
- `updateDisplayPathTiming` fica em `0xC6941E8`.
- `AmdDalHelper::setDisplayMode` fica em `0xC6B3B5A`.

Tabelas usadas por `getPixelInformation`, em torno de
`0xC892370..0xC8923C0`:

- pixel type: `[2, 2, 2]`.
- bits por pixel: `[16, 32, 32]`.
- bits por componente: `[5, 8, 10]`.
- contagem de componentes: `[3, 3, 3]`.
- depth 0: máscaras RGB 5-bpc `0x7C00`, `0x03E0`, `0x001F`.
- depth 1: máscaras 8-bpc `0x00FF0000`, `0x0000FF00`, `0x000000FF`.
- depth 2: máscaras 10-bpc `0x3FF00000`, `0x000FFC00`, `0x000003FF`.

Estrutura relevante de `IOPixelInformation`:

- `+0x00`: bytesPerRow.
- `+0x08`: bitsPerPixel.
- `+0x0C`: pixelType.
- `+0x10`: componentCount.
- `+0x14`: bitsPerComponent.
- `+0x18`: componentMasks.
- `+0x58`: pixelFormat.

Conclusão: nunca altere apenas `bitsPerComponent`. Formato, máscaras, stride,
índice de profundidade e programação DAL precisam continuar coerentes. A
v1.0.11 provou ainda que mesmo remapear o registro completo do IOFramebuffer
não altera o `pBPC` real do link.

### AMDSupport exato

- binário extraído:
  `/Users/arthur/GIT/rx6900xt/backups/ventura/20260913-0423-22H730-kernel-collection/extracted/com.apple.kext.AMDSupport`
- SHA-256:
  `881ff2d93edc2b4eb57f5e31569f789dabb38d717ec5ace7ca7b43ab25b17ba8`.
- versão de fonte observada: 4.14.4.
- três símbolos/arrays locais com nome `BITS_PER_COMPONENT` aparecem em
  `0x142535F0`, `0x14253660` e `0x14253C40`.
- arrays relevantes começam como `[5,8,10,16,16]`; dois têm zero terminador.
- A referência direta mapeada em
  `AtiLineBuffer::ValidateLineBufferForSinglePath` aponta para o terceiro array
  e calcula profundidade de line buffer.
- Isso é validação/cálculo de line buffer, não seleção do formato físico de
  saída. Não patchar esses arrays sem mapear todos os consumidores.

## Estado atual do repositório e da EFI

- Branch de trabalho: `fix-nodcc-no2step-test`.
- HEAD ao criar este documento: `94ad3f488dc98927de5cb31f6aa7d31866b05f57`.
- O HEAD contém código opt-in de experimentos rejeitados para preservar a
  história e permitir análise. Ele não é o release seguro instalado.
- O último estado verificado da EFI, antes de ela ser desconectada, foi rollback
  byte a byte para a baseline 1.0.3 usando os hashes acima.
- Caminhos de destino quando a EFI está montada:
  `/Volumes/EFI/EFI/OC/Kexts/NootRX_fix.kext` e
  `/Volumes/EFI/EFI/OC/config.plist`.
- Sempre confirme montagem e hashes depois da cópia; não confie apenas no
  horário da pasta. Quando necessário, atualize o timestamp visível do bundle
  para evitar confusão, sem usar isso como prova de conteúdo.

## Coletor de diagnóstico

- Script no USB: `/Volumes/EFI/coletar_diagnostico.sh`.
- A coleta normal agora tem somente 8 passos.
- SHA-256 da versão leve:
  `cdd20a003f54d41c11c55f468435d73d9623c1c4ed4cb7c3013bfb13e38d1829`.
- Fonte/cópia local:
  `/Users/arthur/GIT/rx6900xt/backups/efi/20260913-0454-lightweight-diagnostic-collector/coletar_diagnostico.sh`.
- Os antigos passos 9/10 copiavam novamente os stubs AMD e toda a
  `SystemKernelExtensions.kc`; foram removidos do fluxo normal porque a coleção
  22H730 já está preservada.
- Use o coletor completo arquivado somente quando o build do macOS mudar e uma
  nova Kernel Collection for realmente necessária.
- O resumo deve incluir marcadores específicos de novos experimentos, além de
  `[INIT]`, `[FLAGS]`, `[XML]`, `[AGDP]` e `[HWLibs]`.

## Backups e evidência

Raiz obrigatória para backups:

`/Users/arthur/GIT/rx6900xt/backups`

Principais conjuntos:

- diagnósticos por experimento: `backups/diagnostico`.
- snapshots da EFI: `backups/efi`.
- releases experimentais: `backups/releases`.
- Windows/VBIOS: `backups/windows/20260913-0358-reddevil-oc`.
- Ventura/SystemKC: `backups/ventura/20260913-0423-22H730-kernel-collection`.
- diagnóstico da v1.0.11:
  `backups/diagnostico/20260913-0451-v1.0.11-force8bpc`.
- snapshot completo da v1.0.11 observada:
  `backups/efi/20260913-0454-v1.0.11-observed`.

Arquivos diagnósticos históricos existentes, em ordem:

- `20260913-0002-native-vactive-regression`.
- `20260913-0022-pipesplit-avoid-regression`.
- `20260913-0036-single-channel-regression`.
- `20260913-0110-native-passthrough-guard-failed`.
- `20260913-0121-native-passthrough-catastrophic-regression`.
- `20260913-0134-baseline-103-recheck`.
- `20260913-0205-agdp-macpro71-regression`.
- `20260913-0225-force24-failed`.
- `20260913-0242-native-weg-artifacts`.
- `20260913-0301-no2step-regression`.
- `20260913-0310-baseline103-silent-bios`.
- `20260913-0320-baseline103-silent-10min`.
- `20260913-0418-ventura-kext-stubs`.
- `20260913-0451-v1.0.11-force8bpc`.

Não escreva novos backups no pendrive. Não apague ou sobrescreva os backups
existentes. Use `/tmp`, nunca `/private/tmp`, para temporários operacionais e
remova-os depois de preservar qualquer evidência útil.

## Procedimento obrigatório para um próximo experimento

1. Leia este arquivo e o comentário de decisões em `NootRX/NootRX.cpp`.
2. Verifique `git status`; preserve alterações do usuário.
3. Confirme as tags/hashes da 1.0.3.
4. Formule uma hipótese estreita baseada em evidência nova.
5. Documente no código por que o mecanismo é diferente dos testes rejeitados.
6. Mantenha a mudança opt-in e com rollback por remoção de uma única
   propriedade, quando tecnicamente possível.
7. Não toque em MCLK training, `PP_MclkDpmDisabled`, `CFG_FORCEMAXDPM`,
   `rd-no2step`, pipe-split da baseline ou arrays de line buffer por suposição.
8. Compile Release x86_64 e valide `Info.plist`, arquitetura, strings/marcadores
   e hashes.
9. Crie commit e tag específicos do teste; não mova tags históricas.
10. Faça backup local do kext/config atualmente no USB.
11. Altere somente o kext e a chave experimental necessária no config.
12. Valide o config e compare-o com a baseline para provar o diff mínimo.
13. Depois da cópia, compare recursivamente o bundle e valide hashes no USB.
14. Ao concluir o deploy na EFI, sugira sempre ao usuário: "a1 ) deseja ejetar o pendrive para testar?"
15. Teste cold boot, desktop/IINA sem Heaven, Heaven sob carga, fechamento do
    Heaven e idle prolongado.
16. Colete diagnóstico e só então aceite ou rejeite a hipótese.

## Próxima direção de pesquisa, sem autorização para patch imediato

O melhor próximo alvo conceitual é localizar no binário exato 22H730 onde DAL
ou AGDP traduz o timing/EDID para `displaycolorDepth=2` e `pBPC=2`. O experimento
deve mudar o formato real do link e manter coerente o formato da superfície,
não apenas alterar o que `system_profiler` anuncia. Antes de implementar:

- rastreie os consumidores de `displaycolorDepth` e a construção de
  `AmdFbDisplayPath`/stream parameters;
- identifique a origem do `pBPC` observado por AGDP;
- confirme se EDID, colorimetria, range e encoder participam da seleção;
- procure propriedades Apple/DAL existentes antes de criar patch binário;
- prove por disassembly e logging que o ponto escolhido afeta o link real;
- mantenha a 1.0.3 intacta e não implante outro kext apenas por uma coincidência
  de nome ou tabela.

Ainda não há prova conclusiva de que o problema seja exclusivamente um bug do
Ventura, uma política de VBIOS de parceiro ou a interação entre ambos. A placa
é fisicamente estável no Windows, o stack Apple/WhateverGreen puro também
reproduz artefatos no Ventura e a baseline NootRX reduz fortemente o sintoma.
Essa é a fronteira factual atual.
