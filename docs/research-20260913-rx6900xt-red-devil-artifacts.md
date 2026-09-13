# RX 6900 XT Red Devil no Ventura: investigação dos artefatos residuais

Data da investigação: 2026-09-13  
Hardware observado: PowerColor Red Devil RX 6900 XT, `1002:73BF`, revisão `C0`, subsistema `148C:2408`, VBIOS `113-D41201-XT`; X99 com Intel Core i7-6800K; SMBIOS `MacPro7,1`; macOS Ventura 13.7.8.

## Conclusão executiva

A RX 6900 XT usa um chip Navi 21 reconhecido pelo driver da Apple, mas a Red Devil não é uma placa validada pela Apple como um conjunto completo. A própria Apple limita a sua declaração de suporte aos modelos RX 6900 XT fabricados ou vendidos pela AMD, com ID `0x73BF`.[^1] A Red Devil preserva esse ID principal, porém usa PCB, três conectores de energia, quatro saídas, dual BIOS, tabelas de energia e VBIOS próprios da PowerColor.[^2] Portanto, “o chip é nativo” não implica que o firmware e o pipeline de display desta placa parceira tenham sido testados pela Apple.

O Ventura não é, por si só, incompatível com Navi 21: a Apple dá suporte à arquitetura desde o Big Sur 11.4, e o NootRX possui caminho explícito para Ventura.[^1][^3] O conjunto de evidências aponta para uma incompatibilidade específica entre o pipeline de display do Ventura, o VBIOS/board da Red Devil e algumas decisões do fork. A GPU executa Metal e OpenGL em carga alta, enquanto os defeitos aparecem em composição, troca de planos e scanout. Isso torna falha geral do núcleo 3D ou da VRAM uma explicação menos provável.

A baseline 1.0.3 continua sendo o único ponto seguro. Ela eliminou aproximadamente 99,5% dos artefatos ao desativar `GPUDCCDisplayable`, sem sacrificar o desempenho. As versões posteriores que alteraram VActive, pipe split, canal de tarefas ou fizeram passthrough nativo foram piores e estão formalmente rejeitadas no comentário de decisão em `NootRX/NootRX.cpp`.

O próximo experimento de código deve alterar uma única diferença ainda não testada: restaurar a exceção oficial que impede o patch de `AppleGraphicsDevicePolicy` no SMBIOS `MacPro7,1`. O nosso fork removeu essa exceção e força a substituição `board-id` → `board-ix` em todos os Macs. O NootRX oficial evita explicitamente esse patch em `MacPro7,1`.[^4] Em todos os seis boots arquivados do fork, o kernel registrou cinco ocorrências de `vendor modeset callback invalid sequence or interleaving!!`. Isso não prova causalidade, mas é a correlação mais direta e o desvio mais estreito em relação ao upstream.

Não há base técnica honesta para prometer “100%” antes desse teste e de uma validação prolongada. O objetivo correto é chegar a zero artefatos e zero reinicializações observadas em uma matriz de testes reproduzível, sem declarar sucesso com base em poucos minutos de desktop ou benchmark.

## O que o NootRX oficial realmente faz em Navi 21

O NootRX oficial não é um simples habilitador de ID. Mesmo para `0x73BF`, ele:

1. identifica Navi 21 e injeta personalidades próprias do acelerador e framebuffer;
2. substitui a tabela de capacidades DDI e golden settings;
3. carrega firmware PSP, SMU e microcódigos selecionados a partir do pacote do projeto;
4. contorna verificações de versão do firmware SMU;
5. mantém diferenças por versão do macOS, incluindo um caminho `VenturaAndLater`.[^3][^5]

O histórico confirma que firmware é uma variável sensível. Uma atualização para o pacote Adrenalin 24.10.1 foi revertida pelo projeto depois de regressões de congelamento e reprodução de vídeo.[^6][^7] Houve também correções específicas de Navi 21 para corrupção de memória, pânico e tap delays. Logo, o passthrough “totalmente nativo” não é necessariamente mais correto numa placa parceira. No nosso hardware ele foi conclusivamente pior: artefatos severos e dois resets reais do canal GFX/DisplayPipe.

O NootRX se descreve como software não suportado e em pesquisa ativa, com pequenos problemas esperados.[^8] Isso também responde à pergunta “como todos os outros usam normalmente?”: nem todos usam. Há relatos oficiais do projeto de artefatos e freezes que variam conforme GPU, firmware e versão do macOS. Um Navi 21 funcionava perfeitamente no Sonoma e ficou lento e cheio de artefatos no Sequoia;[^9] outro relatório registra checkboards, texto corrompido, freezes e GPU restart em várias versões, com frequência diferente entre sistemas.[^10]

## Evidência local do tipo de falha

### DCC é causa importante, mas não a causa inteira

Na 1.0.3, `GPUDCCDisplayable=false` foi confirmado no IORegistry. A melhora de cerca de 99% para 99,5%, junto ao fato histórico de captura/gravação alterar ou ocultar os artefatos, localiza o problema no caminho de apresentação/scanout. O DCC interno de renderização não foi globalmente removido; a mudança impede superfícies comprimidas de serem apresentadas diretamente pelo display.

### O timeout é do compositor físico de planos

O aviso residual é:

```text
REG_WAIT timeout 1us * 100000 tries - mpc2_assert_idle_mpcc
```

No Display Core da própria AMD, `mpc2_assert_idle_mpcc` espera que `MPCC_IDLE` se torne `1`; o bloco MPCC faz parte do MPC, o compositor de múltiplos planos.[^11] A API descreve essa rotina como a espera para um MPCC entrar em estado ocioso.[^12] Em seguida, os nossos logs mostram power-gating dos front-ends. Nas versões ruins, a mesma área progride para timeout de `IOAccelDisplayPipe`, travamento do WindowServer e reset de canal GFX.

Há um relato independente em hardware AMD no qual artefatos dependem de direct scan-out/pipeline split, somem ao forçar composição e aparecem junto de `mpc2_assert_idle_mpcc` durante a troca de planos.[^13] Ele não prova que o bug Linux seja idêntico ao do macOS, mas reforça a interpretação arquitetural do log: o sinal aponta para transição de planos de display, não para shader ou benchmark 3D.

### AGDP está sendo modificado contra a regra do upstream

O upstream contém desde 2023 a regra:

```cpp
// Don't apply AGDP patch on MacPro7,1
if (strncmp("Mac-27AD2F918AE68F61", BaseDeviceInfo::get().boardIdentifier, 21) == 0) { return; }
```

O fork da baseline 1.0.3 substituiu-a por aplicação incondicional do patch, sob a suposição de que uma Navi comercial precisaria disso. Essa suposição não foi validada e contradiz a decisão específica do mantenedor para `MacPro7,1`.[^4] O boot-arg `agdpmod=pikera` presente no `config.plist` não é processado pelo NootRX; com WhateverGreen desativado, é o código incondicional do fork que altera o binário AGDP. No WhateverGreen, `pikera` significa exatamente trocar `board-id` por `board-ix`, enquanto `agdpmod=ignore` desativa esses patches.[^14]

O aviso `vendor modeset callback invalid sequence or interleaving!!` aparece cinco vezes em cada um dos seis diagnósticos arquivados. Como o aviso é emitido pelo AGDP durante validação e callback de modeset, restaurar a exceção oficial é o primeiro teste de melhor relação entre evidência e risco.

### Rechecagem limpa da 1.0.3 em 2026-09-13 01:34

O binário no EFI foi confirmado como a baseline pelo SHA-256 `fc17f9d4bb9b01a2a8ffb6cd81e967538a4a81694f3b09582d1d58e429ff4629` e MD5 `a7ed288083b54817460828db7cc58c4b`. O diagnóstico foi preservado em `../backups/diagnostico/20260913-0134-baseline-103-recheck`.

No instante da coleta, o boot a 1920×1080/100 Hz/30-bit tinha:

- zero `gpuRestart`, VM fault e timeout de `IOAccelDisplayPipe`;
- dois timeouts `mpc2_assert_idle_mpcc` durante a programação do display;
- cinco avisos AGDP de sequência/interleaving;
- `GPUDCCDisplayable=false` e todos os flags da 1.0.3 efetivamente aplicados.

Esse resultado separa novamente a baseline das regressões catastróficas posteriores: ela recupera estabilidade, mas os dois sinais do caminho de display permanecem mensuráveis.

### Profundidade, resolução e frequência ainda são discriminadores

Os diagnósticos mostram sempre 30-bit (`ARGB2101010`) e `pBPC=0x2`, isto é, caminho de 10 bits por canal. Os primeiros boots arquivados usaram sinal 2560×1080 com pixel clock de 185,58 MHz, equivalente a 60 Hz. Os boots posteriores usaram 1920×1080 com pixel clock de 228,8 MHz, equivalente a 100 Hz. Como software e modo mudaram simultaneamente, não é válido atribuir a piora a 100 Hz; é necessário um teste A/B na própria 1.0.3.

O NootRX tem inclusive um relato aberto no Ventura no qual 60 e 120 Hz funcionam, mas 165 Hz perde vídeo.[^15] Trata-se de Navi 22, portanto serve apenas para confirmar que modo/timing pode ser uma variável do driver, não como prova para esta Navi 21.

## Hipóteses priorizadas

| Prioridade | Hipótese | Evidência favorável | Limitação atual |
|---|---|---|---|
| 1 | Patch AGDP indevido em `MacPro7,1` perturba a sequência de modeset | Desvio exato do upstream; aviso de sequência/interleaving em todos os boots | Ainda não houve boot 1.0.3 sem o patch |
| 2 | Transição MPCC/DisplayPipe ainda usa um caminho de direct scan-out/planos instável | `mpc2_assert_idle_mpcc`, resets DisplayPipe, efeito da gravação, grande melhora com DCC off | Não se identificou ainda uma propriedade segura para desativar apenas esse caminho |
| 3 | Timing de 10 bpc/100 Hz agrava a incompatibilidade da Red Devil | Últimos boots são 10 bpc/100 Hz; problema está no scanout | O defeito também existia, em menor grau, a 60 Hz |
| 4 | VBIOS/dual BIOS da PowerColor não casa integralmente com as tabelas esperadas pela Apple | Apple só valida placas AMD; Red Devil usa board e dual BIOS próprios | Benchmarks e Windows indicam que o hardware é funcional |
| 5 | Firmware injetado pelo NootRX interage mal com esta revisão/VBIOS | Histórico upstream mostra regressões de firmware | A baseline usa a geração revertida/mais estável e o passthrough nativo foi muito pior |
| Baixa | Defeito geral de VRAM/GPU ou PCIe degradado | Poderia produzir quadrados e resets | Carga 3D é estável, o efeito depende da composição e o link observado é Gen3 x16 |

## Plano de validação, uma variável por vez

1. **Reconfirmar a baseline 1.0.3.** Coletar novo diagnóstico sem alterar kext/config e registrar: modo físico, tempo até o primeiro artefato, quantidade de artefatos e resets. Esse boot separa o comportamento real da baseline das regressões posteriores.
2. **A/B de modo sem novo kext.** Ainda na 1.0.3, testar 1920×1080/60 Hz e depois 100 Hz, pelo mesmo cabo e porta, durante o mesmo roteiro. Se 60 Hz zerar o defeito, o próximo trabalho deve mirar timing/profundidade de cor, não gerenciamento de energia.
3. **Próximo kext experimental.** Partir exatamente da tag `1.0.3`; restaurar somente a exceção AGDP oficial para `MacPro7,1`; preservar `GPUDCCDisplayable=false` e todos os demais valores da baseline. O comentário no código deve registrar o aviso observado e a razão do teste.
4. **Critério de aprovação do experimento AGDP.** Confirmar ausência da mensagem do fork que diz ter aplicado o patch; comparar os avisos de interleaving; executar desktop/F11/IINA, Heaven, Geekbench Metal e ciclos de repouso/retorno. Qualquer regressão implica rollback imediato para a tag 1.0.3.
5. **Somente se AGDP não resolver.** Testar profundidade de 8 bpc/24-bit de forma isolada. O WhateverGreen documenta `-rad24`, mas ele conflita com o NootRX e não deve ser simplesmente recolocado na EFI; seria necessário portar e auditar a técnica, ou usar um modo/EDID de teste que negocie 8 bpc.[^14]
6. **Não executar ainda.** Não desativar `IOAccelDisplayPipeCapabilities` nem `TransactionsSupported` sem prova adicional. Esses recursos pertencem ao contrato entre WindowServer e o acelerador; removê-los pode mascarar o defeito ao custo de regressão funcional ou tela preta.

## Critério de “100% estável”

Uma versão só deve substituir a RX 5700 XT de referência depois de cumprir, no mínimo:

- zero artefatos visuais durante 2 horas de desktop, Mission Control/F11, arraste de janelas e vídeo;
- zero `gpuRestart`, `IOAccelDisplayPipe timeout`, VM fault ou tela amarela;
- Heaven e Geekbench Metal/OpenCL concluídos, não apenas iniciados;
- ciclos repetidos de boot frio, reboot e sleep/wake;
- teste no modo de vídeo final de uso, não somente em 60 Hz se o objetivo for 100 Hz;
- comparação do diagnóstico final com a baseline 1.0.3.

## Fontes

[^1]: Apple, [Use an external graphics processor with your Mac](https://support.apple.com/en-us/102363), especialmente a seção RX 6800/6900 e a nota 7: suporte restrito aos modelos RX 6900 XT fabricados ou vendidos pela AMD, ID `0x73BF`.
[^2]: PowerColor, [Red Devil AMD Radeon RX 6900 XT 16GB GDDR6](https://www.powercolor.com/product-detail175.htm): dual BIOS, PCB 14+2 fases, três conectores de 8 pinos e `1× HDMI + 3× DisplayPort`.
[^3]: ChefKissInc, [NootRX.cpp](https://github.com/ChefKissInc/NootRX/blob/master/NootRX/NootRX.cpp): seleção explícita de `VenturaAndLater` e Navi 21.
[^4]: ChefKissInc, [exceção AGDP para MacPro7,1 no NootRX](https://github.com/ChefKissInc/NootRX/blob/master/NootRX/NootRX.cpp#L279-L288).
[^5]: ChefKissInc, código local upstream em `NootRX/HWLibs.cpp`, `NootRX/X6000FB.cpp` e `NootRX/AMDCommon.hpp`; os caminhos injetam firmware e substituem DDI caps/golden settings para Navi 21.
[^6]: ChefKissInc/NootRX, [Regressions after firmware update to Adrenaline 24.10.1 — issue #100](https://github.com/ChefKissInc/NootRX/issues/100).
[^7]: ChefKissInc/NootRX, [revert do firmware Adrenalin 24.10.1](https://github.com/ChefKissInc/NootRX/commit/858f096).
[^8]: ChefKissInc/NootRX, [README oficial](https://github.com/ChefKissInc/NootRX/blob/master/README.md).
[^9]: ChefKissInc/NootRX, [Navi 21 rendering glitches, macOS Sequoia — issue #95](https://github.com/ChefKissInc/NootRX/issues/95).
[^10]: ChefKissInc/NootRX, [Freezing & GPU Restarting — issue #123](https://github.com/ChefKissInc/NootRX/issues/123).
[^11]: Linux/AMD Display Core, [implementação de `mpc2_assert_idle_mpcc`](https://git.zx2c4.com/linux-dev/tree/drivers/gpu/drm/amd/display/dc/dcn20/dcn20_mpc.c?id=b9030780971b56c0c455c3b66244efd96608846d).
[^12]: Linux/AMD Display Core, [documentação da operação `wait_for_idle` do MPC](https://codebrowser.dev/linux/linux/drivers/gpu/drm/amd/display/dc/inc/hw/mpc.h.html#613).
[^13]: ValveSoftware/gamescope, [direct scan-out, pipeline split, artefatos e `mpc2_assert_idle_mpcc` — issue #1368](https://github.com/ValveSoftware/gamescope/issues/1368).
[^14]: Acidanthera, [WhateverGreen README: `-rad24` e modos `agdpmod`](https://github.com/acidanthera/WhateverGreen#boot-arguments).
[^15]: ChefKissInc/NootRX, [HZ issue no Ventura — issue #111](https://github.com/ChefKissInc/NootRX/issues/111).
