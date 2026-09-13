# Plano futuro: evoluir a 1.0.3 até zero artefatos e estabilidade comprovada

Data: 2026-09-13

Estado: planejamento; nenhuma alteração comportamental autorizada por este arquivo

Baseline: tags `1.0.3` e `v1.0.3-nodcc-fix`, commit `429c6ab`
Memória técnica obrigatória: `AGENTS.md`

## Documento substituído

Este plano substitui apenas a seção “Plano de validação, uma variável por vez”
de `docs/research-20260913-rx6900xt-red-devil-artifacts.md`. As evidências e
fontes daquele documento continuam úteis, mas suas recomendações AGDP e
`-rad24` ficaram obsoletas após os testes 1.0.8, 1.0.9 e 1.0.11.

## Resultado pretendido

Produzir uma nova versão baseada na 1.0.3 que possa substituir a RX 5700 XT no
uso diário, preservando desempenho e eliminando:

- todos os fragmentos visuais observáveis;
- telas amarelas ou pretas anormais;
- travamentos e resets do display/GPU;
- demora de inicialização introduzida pelo kext;
- regressões em Metal, OpenGL, vídeo, sleep/wake ou treinamento GDDR6.

“100% estável” não significa uma garantia matemática. Neste projeto significa
zero falhas observadas em uma matriz de validação definida, longa e repetível,
com logs que confirmem ausência de resets/timeouts e desempenho dentro da
margem da baseline. Uma impressão visual de poucos minutos não é suficiente.

## Estado de partida obrigatório

O ponto de partida de todo teste comportamental é o binário/config exatos da
1.0.3:

- kext SHA-256:
  `fc17f9d4bb9b01a2a8ffb6cd81e967538a4a81694f3b09582d1d58e429ff4629`;
- config SHA-256:
  `f69b1cdfd2e5c9b4827c458ed8ff93d79c5bc246aeee7feda3daeffd3557921c`;
- backup:
  `/Users/arthur/GIT/rx6900xt/backups/efi/20260913-0438-before-v1.0.11-force8bpc`.

Nenhum candidato pode incluir `rd-no2step`, `rd-force8bpc`, bloqueio de MCLK,
mudança de pipe split, passthrough, AGDP 1.0.8 ou outro experimento rejeitado.

## Hipóteses ainda abertas, por prioridade

| Prioridade | Hipótese | Evidência atual | Prova necessária |
| --- | --- | --- | --- |
| P0 | A profundidade/formato real do link 10-bpc agrava o scanout da Red Devil no Ventura | Windows estável em SDR 8-bpc; Ventura usa `displaycolorDepth=2`/`pBPC=2`; 1.0.11 só alterou a superfície e não o link | Localizar e observar quem escreve a profundidade no stream DAL; depois obter um boot em que DAL e AGDP confirmem 8-bpc real |
| P0 | Uma transição de planos/MPCC/direct scanout em baixa carga corrompe tiles | Heaven e gravação mascaram o defeito; tile some ao redesenhar; `mpc2_assert_idle_mpcc` aparece no caminho | Correlacionar artefato com promoção/remoção de planos e estado MPCC, sem usar captura de tela como observador |
| P1 | Timing/porta/EDID a 100 Hz seleciona uma política frágil | Profundidade e timing são derivados da capacidade do sink; há histórico upstream de sensibilidade a refresh rate | A/B controlado 60/100 Hz, DP/HDMI e sink diferente na 1.0.3, medindo o link real |
| P1 | Tabelas de conector/display do VBIOS parceiro divergem do perfil Navi 21 esperado pela Apple | Chip é 73BF, mas board/VBIOS são PowerColor; Windows é estável | Diff estrutural contra ROM de referência AMD 6900 XT compatível, sem flash e sem substituir PowerPlay |
| P2 | Firmware/política específica do Ventura 22H730 contém uma regressão para esse board | Stack Apple/WEG puro também falha; sintomas variam por macOS em relatos do NootRX | A/B do mesmo hardware/baseline em outro macOS, em volume separado, sem atualizar o sistema principal |
| P3 | Estado de potência de bloco de display, não UCLK bruto, falha na saída de carga | Heaven altera o estado; Windows tolera UCLK baixo; clamps anteriores pioraram | Telemetria temporal de DCEF/FCLK/display power-gating antes e depois de fechar Heaven |

As prioridades P0 devem ser investigadas antes de qualquer novo ajuste de
energia ou firmware.

## Princípios de execução

1. Primeiro observar; depois alterar.
2. Uma variável comportamental por candidato.
3. Cada hipótese precisa de um sinal positivo esperado e de um critério de
   rejeição antes da compilação.
4. O kext deve falhar fechado em versões/UUIDs desconhecidos do driver.
5. Não usar nomes de símbolos duplicados sem mapear o código proprietário.
6. Não considerar `system_profiler` prova do link; DAL e AGDP precisam concordar.
7. Não usar gravação/screenshot como prova visual, pois a própria captura muda
   o comportamento. Usar observação humana padronizada ou câmera externa.
8. Toda conclusão deve apontar para logs, hashes e diretório de diagnóstico.

## Fase 0 — congelar a referência e criar o protocolo

Objetivo: tornar os próximos resultados comparáveis.

Tarefas:

1. Criar branch experimental a partir da tag `1.0.3`.
2. Levar para a branch somente `AGENTS.md`, este plano e comentários de
   documentação necessários; não levar código comportamental 1.0.4–1.0.11.
3. Produzir novamente o binário da baseline apenas para entender se o toolchain
   atual gera bytes/estrutura diferentes. Não substituir o binário aprovado por
   essa recompilação.
4. Medir cinco boots frios e cinco reboots da 1.0.3 sem mudança, registrando:
   tempo do picker ao login, tempo até o primeiro artefato, modo/porta/BIOS,
   resets e desempenho.
5. Criar uma ficha simples por teste com identificador, hashes, hipótese,
   variável alterada, roteiro e resultado.
6. Definir uma área fixa da tela e um roteiro visual repetível: desktop, barra
   de menu, arraste de janelas, Mission Control/F11 e IINA.
7. Posicionar câmera externa para registrar a tela sem ativar o caminho de
   captura do macOS.

Gate de saída:

- distribuição de tempo de boot conhecida;
- taxa residual da 1.0.3 documentada;
- protocolo repetível e sem mudança de driver durante a medição.

## Fase 1 — A/B físico e de modo, sem novo kext

Objetivo: descobrir se o defeito depende do link/timing antes de patchar DAL.

Executar todos os testes com a 1.0.3, um por cold boot:

1. SILENT, porta atual, 100 Hz.
2. SILENT, mesma porta/cabo, 60 Hz.
3. OC após desligamento total, mesma porta/cabo, 100 Hz.
4. OC após desligamento total, mesma porta/cabo, 60 Hz.
5. DisplayPort versus HDMI, mantendo resolução e frequência quando possível.
6. Outro cabo certificado.
7. Outro monitor/TV com 8-bpc conhecido, se disponível.
8. Mesmo monitor com HDR/desempenhos avançados explicitamente desligados.

Para cada boot, coletar `system_profiler`, IORegistry, logs DAL/AGDP e observar
`displaycolorDepth`, `pBPC`, encoder, pixel clock, colorimetria e range.

Interpretação:

- se um modo produzir `pBPC` diferente e zerar fragmentos, priorizar seleção de
  link/EDID;
- se 60 Hz zerar o problema sem mudar BPC, priorizar timing/bandwidth;
- se DP e HDMI divergirem, priorizar connector/link encoder;
- se tudo reproduzir igual, priorizar planos/MPCC e política do compositor.

Gate de saída:

- pelo menos uma variável física discriminou o defeito, ou todas foram
  formalmente descartadas pelo mesmo roteiro.

## Fase 2 — versão 1.0.12 somente de instrumentação

Objetivo: localizar o proprietário real de `displaycolorDepth` e o evento que
antecede o artefato/reset. Esta versão não deve mudar decisões do driver.

Implementação planejada:

1. Partir do código da 1.0.3, mantendo todas as propriedades iguais.
2. Restringir a instrumentação a Ventura `22H730` e ao UUID
   `E116BA99-722F-3D23-BC50-43C9564FADB7`, ou validar assinaturas inequívocas.
3. Mapear por disassembly e xrefs, antes de rotear:
   - construção de `AmdDetailedTimingInformation`;
   - `updateDisplayPathTiming`;
   - construção de `AmdFbDisplayPath`;
   - `AmdDalHelper::prepareDalDisplayStreamParameters`;
   - `AmdDalHelper::setDisplayMode`;
   - ponto que escreve/lê `displaycolorDepth` antes de `dc_commit_streams`.
4. Logar somente leitura, com timestamp/contador e rate limit:
   - modo, refresh/pixel clock, encoder, BPC/color depth;
   - formato da superfície, bits/componentes e masks;
   - número de planos/pipes e eventos de promoção/remoção quando identificados;
   - estados de display clocks/power-gating em torno da saída do Heaven.
5. Preservar logs em ring buffer e no arquivo existente sem bloquear o
   modeset. Não fazer `IOSleep`, alocação grande ou logging por frame.
6. Adicionar marcador único provando que cada rota de telemetria foi aplicada.
7. Comparar o boot da 1.0.12 com a distribuição da Fase 0. Instrumentação que
   aumenta significativamente o boot ou muda artefatos é invasiva e deve ser
   redesenhada.

Proibido nesta fase:

- sobrescrever campos;
- remapear depth;
- injetar propriedade nova de DAL/SMU;
- alterar EDID, clocks, planes ou AGDP.

Gate de saída:

- cadeia causal documentada da capacidade do sink/timing até o
  `displaycolorDepth=2`;
- ponto exato em que DAL recebe o valor;
- confirmação de se `pBPC` é derivado do mesmo valor;
- ao menos um sinal temporal de planos/MPCC ou sua exclusão.

## Fase 3 — engenharia reversa dirigida

Objetivo: transformar os dados da Fase 2 em um candidato estreito.

### Trilha A: profundidade real do link

1. Buscar primeiro propriedades existentes nos XMLs Apple/NootRX e strings
   `Dal*` que limitem output BPC ou deep color.
2. Mapear cada propriedade por xref até uma leitura real no driver; não testar
   apenas por semelhança de nome.
3. Determinar a codificação real de 8-bpc nos logs. Não assumir que `1` é 8-bpc
   sem observar um modo de referência.
4. Identificar todos os consumidores do campo para manter coerentes:
   superfície IOFramebuffer, stream DAL, link encoder, line buffer e AGDP.
5. Preparar um patch conceitual que altere o valor uma única vez antes da
   construção final do stream, em vez de enganar APIs públicas separadamente.

### Trilha B: planos/MPCC

1. Localizar criação, attach/detach e espera idle dos MPCCs usados no caminho
   ativo.
2. Correlacionar número de planos com IINA, desktop, gravação e Heaven.
3. Verificar se `GPUDCCDisplayable=false` ainda permite direct scanout ou
   promoção de overlay em outro estágio.
4. Procurar propriedade oficial para forçar composição ou desativar apenas a
   promoção problemática, preservando `IOAccelDisplayPipe` e transactions.
5. Não remover `DisplayPipeSupported` ou `TransactionsSupported`; isso quebra o
   contrato com WindowServer e pode apenas transformar artefato em tela preta.

### Trilha C: VBIOS/connector

1. Obter uma ROM de referência da AMD RX 6900 XT com mesmo device ID e versão
   comparável, de fonte verificável.
2. Fazer diff semântico somente de Connector Object Info, GPIO/I2C/AUX,
   display caps e tabelas relacionadas a DCN/link.
3. Não copiar PowerPlay inteiro e nunca fazer flash.
4. Se uma diferença tiver consumidor comprovado no driver Apple, considerar
   injetar somente o campo necessário, opt-in e reversível.

Gate de saída:

- revisão externa/segunda opinião do mapa de dados e do ponto de patch;
- prova de que o candidato alcança o mecanismo real e não repete 1.0.9/1.0.11;
- diff comportamental planejado de uma única variável.

## Fase 4 — primeiro candidato comportamental

O número sugerido é 1.0.13; 1.0.12 fica reservado para instrumentação.

Escolher somente uma das opções abaixo conforme a evidência:

### Candidato A — limitar o link a 8-bpc de forma coerente

Somente se a Trilha A mapear a origem real do valor.

Critérios técnicos antes de instalar:

- DAL deve registrar a profundidade correspondente a 8-bpc;
- AGDP deve registrar `pBPC` correspondente a 8-bpc;
- `system_profiler` deve anunciar ARGB8888;
- pixel format, masks, stride e line-buffer devem concordar;
- nenhuma chamada deve continuar tratando o mesmo stream como 10-bpc.

Se DAL ou AGDP continuar em 10-bpc, o teste é inválido e não deve avançar para
avaliação visual.

### Candidato B — impedir somente a promoção de plano/scanout problemática

Somente se a Trilha B correlacionar o defeito com planos/MPCC.

Critérios técnicos:

- preservar aceleração e o contrato IOAccelDisplayPipe;
- não alterar clock/memória;
- reduzir o caminho a composição estável sem desativar o framebuffer;
- provar por marcador/log que a promoção específica deixou de ocorrer.

### Candidato C — normalizar um campo de display do board

Somente se a Trilha C demonstrar uma diferença de VBIOS com consumidor real no
driver Apple.

Critérios técnicos:

- injetar o menor campo possível;
- não substituir firmware/PowerPlay completo;
- não fazer flash;
- manter rollback por remoção de uma propriedade.

Gate de saída da Fase 4:

- cold boot sem atraso anormal;
- zero fragmentos no roteiro curto, observado externamente;
- zero timeout/reset/fault;
- comportamento claramente melhor que a 1.0.3.

Qualquer falha encerra o candidato, gera tag `experiment-...-failed`, registra
o resultado no código/AGENTS e restaura a 1.0.3.

## Fase 5 — A/B de versão do macOS, se necessário

Executar somente se as Fases 1–4 não isolarem uma correção.

1. Instalar um macOS alternativo suportado em volume/disco separado.
2. Não atualizar nem alterar o Ventura principal.
3. Usar o mesmo hardware, porta, cabo, modo, BIOS e baseline equivalente.
4. Executar o roteiro externo e coletar logs.
5. Se outro macOS zerar o problema, comparar versões de
   `AMDRadeonX6000Framebuffer`, `AMDSupport`, firmware e propriedades.
6. Backportar somente uma diferença comprovada e compatível; não trocar todos
   os binários/firmwares em bloco.

Esta fase responde se o Ventura 22H730 é parte necessária do problema, mas um
resultado positivo em outro macOS não autoriza automaticamente portar código
proprietário nem misturar kexts Apple entre sistemas.

## Matriz de validação para uma candidata a release

### Inicialização e energia

- 10 cold boots após desligamento total.
- 20 reboots.
- 20 ciclos sleep/wake.
- cinco ciclos de troca 60 -> 100 -> 60 Hz.
- zero falha de treinamento GDDR6.
- mediana e pior tempo até login dentro de 20% da baseline medida na Fase 0.

### Interface e vídeo

- duas horas contínuas de desktop, barra de menu e arraste de janelas.
- 100 ciclos de Mission Control/F11.
- duas horas de IINA em `Auto (Copy)` com conteúdo variado.
- observação por câmera externa; não usar screen recording como único teste.
- zero fragmento transitório ou persistente.

### Transição de carga

- 10 ciclos: abrir Heaven, manter cinco minutos, fechar e observar idle por dez
  minutos.
- Geekbench Metal concluído três vezes.
- Geekbench OpenCL/OpenGL aplicável concluído três vezes.
- oito horas de carga mista/idle.
- 24 horas de idle final, porque o defeito é mais visível fora de carga.

### Logs

- zero `IOAccelDisplayPipe timeout`.
- zero restart de canal/GPU.
- zero `VM_FAULT`.
- zero tela amarela/preta ou recuperação do WindowServer.
- nenhum crescimento do contador `GPU Restart Count`.
- warnings `mpc2_assert_idle_mpcc` devem ser contados e comparados, mas sozinhos
  não reprovam sem falha correlata.

### Desempenho e função

- Metal/OpenGL dentro de 5% da mediana da 1.0.3 no mesmo BIOS/modo.
- reprodução de vídeo, áudio HDMI/DP, mudança de resolução e hotplug normais.
- nenhuma regressão perceptível de UI, consumo ou temperatura.
- validação final no modo realmente desejado pelo usuário, especialmente
  100 Hz; sucesso apenas a 60 Hz não cumpre o objetivo final.

## Critério de promoção

Um candidato só vira nova baseline quando:

1. cumprir toda a matriz sem artefato/reset;
2. produzir diagnósticos completos e hashes reproduzíveis;
3. manter diff mínimo e documentado em relação à 1.0.3;
4. sobreviver a uma rechecagem em dia/boot diferente;
5. manter a 1.0.3 disponível para rollback;
6. receber tag de release nova sem mover tags antigas.

Até lá, todas as versões devem ser nomeadas e tratadas como experimentais.

## Critérios de parada imediata e rollback

Restaurar a 1.0.3 após qualquer um destes eventos:

- kernel panic ou falha de treinamento GDDR6;
- tela preta prolongada, tela amarela ou login anormalmente lento;
- primeiro reset de display/GPU;
- aumento evidente de fragmentos;
- perda de aceleração ou queda superior a 10% em teste rápido;
- logs provando que o mecanismo pretendido não foi realmente alterado;
- necessidade de combinar dois patches para “ver se funciona”.

O rollback deve usar os arquivos preservados, ser verificado pelos hashes e
ser registrado no resultado do experimento.

## Artefatos esperados por fase

Cada fase deve entregar:

- comentário de decisão no código;
- commit e tag específicos;
- kext/config e manifesto com hashes;
- backup local pré-deploy;
- pasta de diagnóstico pós-teste;
- tabela curta: esperado, observado, aprovado/rejeitado;
- atualização de `AGENTS.md` e deste plano quando a prioridade mudar.

## Primeiro trabalho concreto quando a implementação for retomada

1. Executar Fase 0 e Fase 1 sem criar outro kext comportamental.
2. Criar a 1.0.12 de instrumentação somente depois dos A/Bs físicos.
3. Usar o binário extraído 22H730 para rastrear xrefs do texto/log
   `displaycolorDepth` até o campo do stream.
4. Instrumentar `updateDisplayPathTiming`, preparação de stream DAL e
   `AmdDalHelper::setDisplayMode` com leitura apenas.
5. Reunir segunda opinião antes de escolher a primeira escrita.

Essa ordem é deliberada: os testes anteriores falharam principalmente quando
uma propriedade ou tabela plausível foi tratada como causa sem provar que ela
controlava o estado real do link. O próximo avanço deve nascer da cadeia de
dados observada, não de mais uma tentativa por nome.
