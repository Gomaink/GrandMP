# GrandMP — roadmap oficial de produto

Estado de referência: **2026-10-13**. Números abaixo são marcos propostos, sem datas prometidas. Planejamento não autoriza implementação, instalação de ASI, execução de GTA ou publicação. Cada trabalho futuro precisa de escopo aprovado; um marco fecha com comportamento de produto demonstrado, não com a conclusão de seus laboratórios.

GrandMP será um multiplayer próprio para GTA San Andreas clássico: Windows primeiro, servidor independente, Pawn e compatibilidade progressiva de API SA-MP; Android funcional após 1.0. Desde agora, dados de rede, domínio e interfaces permanecem portáteis para ambas as plataformas. Não há objetivo implícito de interoperar com o protocolo ou os binários de plugins SA-MP.

## Estado real, confrontado com o código

| Capacidade | Estado hoje | Evidência e limite |
|---|---|---|
| Core C++20, versão e logging | Implementado e testado | `shared/`; versão no CMake ainda `0.2.0`, diferente de protocolo v1 e da numeração deste roadmap. Não alterar versão por documentação. |
| Identificação Hoodlum / ASI | Funcional no diagnóstico homologado | `asi_entry.cpp`, `executable_detection.cpp`; fingerprint completo, `InitializeASI` e log reais. Runtime temporário, PIN desabilitado, sem transporte ou backend de entidades no ASI. |
| Leitura externa de CJ | Funcional no smoke Windows x86 informado pelo usuário | `position_observer.cpp`: `ReadProcessMemory`, matriz/fallback, valores finitos, saída do processo. Não é API Pawn nem escrita de posição. Ctrl+C com GTA ainda aberto foi pendência explícita anterior; validar antes de fechar ciclo do cliente nativo. |
| Transporte / sessões / replicação XYZ | Implementado e smoke 0.3.1–0.3.2 aprovado pelo usuário | `protocol.hpp/.cpp`, `sessions.hpp/.cpp`, `observer_network.cpp`, cliente sintético. UDP IPv4, loopback, sessão ligada ao endpoint; nonce/sessão não são autenticação. Servidor retransmite posições do cliente, não é autoridade de gameplay. |
| Cena e roster remotos | Implementados, testados e smoke textual 0.3.3 aprovado | `RemoteRoster`, `RemotePlayerScene`: 63 remotos, XYZ, interpolação 180 ms, teleport acima de 35 unidades. Spawn/pose/despawn no terminal; nenhum ped no GTA. |
| Fila / adapter / backend simulado | Implementados e testados em memória | `QueuedRemotePlayerAdapter`: fila 256, geração, owner thread explícito, fail-closed, retry e cleanup; `pump()` executa backend fora do mutex. Slice 0.3.6/2 conecta ObserverNetwork → roster/cena → adapter → backend simulado no host e testa o servidor real em loopback. Não existe `IRemotePlayerEntityBackend` GTA concreto, callback homologado ou budget de frame medido. |
| Lifecycle, DLL e bootstrap experimental | Laboratórios reais isolados concluídos | Hosts + DLL/ASI experimental; quiescência de dispatcher cooperativo e referência própria de módulo. Não demonstram inscrição/removal de callback no GTA. |
| Contexto de execução GTA | Evidência física parcial | CALL `0x53E981` transferiu para `0x53BEE0`; fase menu ambígua. LoaderLock livre no snapshot `20261013-002726` em InitializeASI. Não comprova instalação concorrente, ownership de patch, afinidade para APIs do motor ou callback seguro. |
| Ponte observer → runtime ASI | **Inexistente** | Cena e rede rodam em processo externo; queue no core não atravessa processos. Este é um requisito explícito de 0.3.6/0.4. |
| Gameplay autoritativo, veículos, combate e UI | **Inexistentes** | Não há simulação/autoridade de saúde, armas, dano, veículos ou apresentação UI em rede. |
| Pawn / AMX / natives / API SA-MP | **Inexistentes no produto** | Não há VM, compilador fixado, dispatcher AMX, gamemode ou bindings. Catálogo documental é planejamento; todos os itens PENDING. |
| Assets próprios / Android | **Inexistentes no produto** | Não há pipeline modding/cache/distribuição nem cliente NDK/JNI funcional. |

Últimos resultados históricos informados/documentados: 0.3.5 Slice 3 **106/106 Win32 e 97/97 x64**; core/servidor anteriores GCC/Clang 20/20 cada no escopo daquele momento. Parser LoaderLock 49/49 offline. Esses resultados não foram reexecutados por este planejamento e não certificam a árvore atual em Linux/Android. Evidências: [remote-player-foundation](remote-player-foundation.md), [native-adapter-foundation](native-adapter-foundation.md), [asi-bootstrap-foundation](asi-bootstrap-foundation.md), [snapshot físico e relatório auditável](gta-bootstrap-readiness.md). Smoke físico declarado pelo usuário é distinto de execução automatizada ou independente do agente.

## Trilhas paralelas e matriz de dependências

| ID / capacidade | Responsável | Pré-requisitos | Libera |
|---|---|---|---|
| N1 — execução residente no contexto GTA | Windows | Fingerprint, mecanismo específico, concorrência/ownership, bootstrap/residência, forwarding original | 0.3.6 contador; N2/N3 |
| N2 — entrega local da cena ao ASI | Windows + core | N1; envelope limitado, identidade/geração, fila existente, lifecycle entre processos | 0.3.6 ponte; 0.4 cena real |
| N3 — entidade ped nativa | Windows | N1/N2; ABI/modelo/criação/destruição verificados por edição | 0.4 |
| R1 — dois clientes reais | Client + server | N3; sessões/replicação, caminho local da cena, escopo de conectividade autorizado | 0.5 |
| G1 — domínio e autoridade de gameplay | Server + core | Identidade, ownership, ações validadas, schema/versionamento explícitos | 0.6; natives de gameplay |
| G2 — backend veículos/combate/UI | Windows | N1/N3; G1; ABI e recursos por capacidade | 0.6; parte de 0.8 |
| S1 — VM/compilador Pawn, AMX e scheduler | Scripting + server | Escolha/licença/revisões/cell ABI; contratos portáteis e budgets | Pode começar em paralelo a N1; base 0.7 |
| S2 — bindings/callbacks e gamemode | Scripting + server | S1; domínio exigido por cada item, G1/G2 para demo completa | 0.7/0.8 |
| C1 — perfil compatível e testes por item | Scripting + docs | Registro fixado; S1/S2; requisitos de backend do item | Pode especificar/testar assinatura cedo; fecha 0.8 por perfil |
| M1 — recursos/assets/modding | Assets + server + Windows | Manifesto/cache/licenças/limites, G1/G2; catálogo DL como referência, sem wire SA-MP | 0.9 |
| O1 — release/reprodutibilidade/operação | Todos | Escopo 1.0 publicado, gates dos itens nele incluídos | 1.0 |
| A1 — cliente Android nativo | Android + core | Toolchain e versão GTA Android fixados; adaptador próprio, backend e execução seguros | Core/contratos revisados cedo; produto pós-1.0 |

Números de release não são dependência linear: S1 e testes puros de servidor podem avançar durante N1; primeiros bindings sem engine podem ficar prontos antes dos veículos. A gamemode de 0.7 só fecha quando suas capacidades funcionarem. Perfil C1 não exige cobrir todos os itens do inventário. M1 pode projetar manifesto/cache antes de 0.9, mas renderização/distribuição final exige backends reais. Android permanece consumidor de wire/domain portáveis; não reutilizar offsets/layouts PC.

**Caminho prioritário para a ponte:** manter temporariamente o observer como dono da rede/cena e entregar comandos por IPC local limitado ao ASI residente (candidato Windows: named pipe com escopo/permissão local). O consumidor apenas enfileira valores; só o callback homologado chama backend. Nada de structs C++ cruas, ponteiros GTA, chamada de engine na thread do pipe ou segunda implementação concorrente da rede. IPC precisa de versão própria, cap de payload/fila, associação ao processo/sessão e descarte de geração antiga. Esta escolha é proposta de arquitetura a ratificar no slice N2, não código existente; Android pode ligar sua cena diretamente à fila no próprio processo. Após cliente nativo completo, retirada do observer é uma decisão de produto separada.

## Política comum de aceitação

Cada marco entrega: executável/feature demonstrável, teste repetível, escopo/versionamento, relatório de limitações, rollback e evidências identificadas por build/configuração. Gate mínimo: testes Win32/x64 afetados, portable core/servidor em Linux quando toolchain disponível, e smoke manual que exercite o produto daquele marco. Falha crítica interrompe o marco; não abrir outro laboratório genérico nem promover versão para contornar o bloqueio. Investigações existentes são reutilizadas; medir novamente apenas quando houver pergunta nova ou mudança de binário/configuração.

Gates nativos: identidade + bytes/ABI comprovados; callback correto sem chamadas fora de thread autorizada; instalação sem instrução parcial em execução; residência de código/estado; original executado; conflito de terceiro recusado; saída sem restauração insegura em DllMain. `Events::Remove()` não restaura automaticamente CALL. Fonte escolhida anteriormente (`plugin-sdk::Events::gameProcessEvent`) continua candidata, não homologada. Não substituir esses gates por coincidência de TID, ausência de Ldr frames, shared_ptr ou sucesso de host cooperativo.

Todos os budgets abaixo são **critérios propostos para homologação**, não desempenho medido hoje. Fixar hardware/build/carga no relatório. Um requisito que não puder ser demonstrado permanece NO-GO. Relatório físico precisa distinguir menu visualmente confirmado, loading, gameplay e término; não inferir fase pelo nome do arquivo.

## Alpha 0.3.6 — integração nativa mínima e controlada

- **Objetivo/entrega:** um ASI experimental separado, opt-in e desativado por padrão, com bootstrap residente e um callback contador comprovado; ponte local recebe a cena existente e drena comandos contra backend simulado/inativo no contexto homologado. Sem ped ou gameplay.
- **Dependências:** N1/N2; fechamento das dúvidas específicas de instalação e vida útil. LoaderLock já medido não será repetido como entrega. Budget próprio do callback contador: p99 ≤ 0,5 ms no host publicado, sem I/O bloqueante no frame; sessão de 30 min e 10 ciclos de entrada/saída sem chamada a estado destruído. O custo da rotina original não integra o budget do contador.
- **Aceitação/testes:** teste concorrente/falhas/trampoline ou wrapper da solução escolhida; recusa de bytes/fingerprint/conflito; residência quando removida referência externa **somente no host descartável** (no GTA, não chamar FreeLibrary da referência do loader; duração até término do processo); dispatch após desativação lógica não chama trabalho; fronteira de sessão na ponte, fila cheia e reinício do observer; shutdown no owner thread. Resolver/medir o risco do mutex antes de backend de engine.
- **Smoke físico:** autorizado separadamente, instalação Hoodlum isolada, loader 7.7.0 e somente ASIs conhecidos. Menu → load → gameplay → nova partida → sair; contador/thread/contexto e chegada/limpeza de comandos lógicos sem entidades. Rollback: desabilitar opt-in, encerrar processo e retirar artefato experimental; não hot-unload.
- **GO:** original preservado, instalação justificável, residência e contexto demonstrados, ponte limitada e sem frame bloqueado. **NO-GO:** qualquer gate de patch/concorrência/lifecycle não resolvido. Entrega não é outro manager de hooks; se instalação não puder ser defendida, publicar o único impedimento técnico e parar.

## Alpha 0.4 — primeiro ped remoto visível

- **Objetivo/entrega:** um personagem remoto 3D criado pela `RemotePlayerScene`, com pose XYZ, teleport e remoção; fonte inicial é cliente sintético, usando a ponte de 0.3.6. Modelo local fixado/documentado, não novo campo fictício no UDP atual.
- **Dependências:** N1/N2/N3; evidência por Hoodlum de alocação, world-ready/modelo, inserção/remoção e ownership. Criação parcial deve ser revertida no contexto correto, sem crash masking genérico.
- **Aceitação/testes:** backend create/update/remove idempotentes, IDs por sessão, falha de modelo/criação, ausência de mundo, reset/reconexão, comandos antigos e fila cheia. Nenhuma entidade remanescente após despawn/cleanup confirmado. Core/cena não ganha offsets GTA. Erro visual estacionário ≤ 0,25 unidade GTA em ambiente sem atraso artificial, como budget inicial.
- **Smoke físico:** GTA real + sintético; ver exatamente um ped, mover fonte, confirmar posição/altura, teleport, despawn, reconnect, troca de partida e saída normal. Registrar vídeo/log e distinguir ped do CJ local.
- **GO:** ped é visível e pertence à sessão correta; lifecycle e falhas conservadoras demonstrados. **NO-GO:** apenas logs spawn/pose, entidade parcial, sujeira ao reset ou chamadas de engine não homologadas. Rotação/animação completa não é critério deste primeiro ped; a limitação é publicada.

## Alpha 0.5 — dois clientes GTA reais sincronizados

- **Objetivo/entrega:** A vê B e B vê A com IDs corretos, movimento XYZ, entrada/saída, teleport e reconstrução após reconnect; sem depender do sintético para representar o segundo jogador.
- **Dependências:** R1 + 0.4; captura real local e ponte nos dois clientes. Publicar como as duas instâncias se conectam: mesma máquina/ambiente isolado ou LAN explicitamente autorizada. O servidor atual só faz bind em loopback; habilitar LAN exige slice de endpoint/configuração e avaliação de risco, não abrir firewall automaticamente.
- **Aceitação/testes:** duas sessões reais distintas; reinício de cliente/servidor, snapshot/tombstones e atraso/reordenação sem reviver jogador antigo; soak 60 min. Em perfil proposto com perda 5%, RTT 150 ms/jitter ±30 ms e reordenação 10%, recuperação de posição e despawn dentro do timeout publicado; medir jitter/erro e fixar orçamento antes do GO. Orientação/estado adicionais exigem source e schema explícitos quando incluídos.
- **Smoke físico:** dois GTA legítimos e isolados, ambos andando/alterando altura/teleportando e alternando término/reconnect. Ver movimento remoto nas duas telas, não somente terminal. Encerrar servidor e verificar limpeza/reconexão controlada.
- **GO:** experiência bidirecional real e recovery mensurados. **NO-GO:** só GTA + sintético, sessão ambígua, ghosts, suspensão de frame ou alegação de robustez pública para UDP não autenticado.

## Alpha 0.6 — gameplay essencial

- **Objetivo/entrega:** escopo mínimo publicado: movimento/estado on-foot, spawn/morte/respawn, um fluxo driver/passenger de veículo e combate básico com saúde/arma/dano decididos ou validados pelo servidor. Escolher subconjunto de armas/veículos; não prometer toda a física.
- **Dependências:** G1/G2 + 0.5. IDs de entidades, ownership, mundo/interior e comandos/eventos ordenados. Protocolo deve ser versionado/capability-negotiated na tarefa que acrescentar campos; hoje só XYZ é enviado. Veículos e combate podem ser equipes independentes sobre o mesmo domínio.
- **Aceitação/testes:** entrar/sair/trocar driver/passenger, veículo criado/removido, desync/reconnect, spawn/dano/morte única e respawn; valores/ações inválidos e duplicados recusados; ações de cliente não se tornam autoridade por mera retransmissão. Documentar decisões de física/latência e ordem de eventos.
- **Smoke físico:** dois GTA com disputa/saída de veículo, deslocamento, tiro/dano/morte/respawn, troca de interior e disconnect durante cada ação; comparar estado autoritativo e apresentação nas duas telas.
- **GO:** casos essenciais funcionam e não há duplicação de dano/entidade/ownership. **NO-GO:** relay de saúde/combate sem regra de autoridade, estados inconsistentes persistentes ou expansão implícita do protocolo.

## Alpha 0.7 — runtime Pawn e primeira gamemode

- **Objetivo/entrega:** compilador/VM fixados e licenciados, AMX com ABI/cell size e versões declaradas, scheduler servidor e gamemode pequena reproduzível: entrar → escolher/spawn → mover/usar veículo → combate/morte/respawn, chat/timer básico no subset publicado. S1 e natives de servidor puro podem começar antes de 0.6.
- **Dependências:** S1/S2; somente capacidades utilizadas pela gamemode dependem de G1/G2/UI. VM não pode chamar engine GTA no processo servidor; scripts produzem comandos de domínio e eventos para clientes.
- **Aceitação/testes:** compilar script e carregar AMX válidos, recusar arquivo malformado/módulo/native não suportado, testar referências/strings/floats/retornos, budgets de instruções/tempo, erros de script e teardown com timer pendente. Log de ordem/propagação de callbacks conforme contrato do subset. Plugin binário SA-MP fora desse gate.
- **Smoke físico:** servidor inicia gamemode real, dois GTA executam fluxo da demo; reload somente se lifecycle explicitamente suportado, caso contrário reinício controlado. Erro de script não trava loop de sessões.
- **GO:** script realmente governa ações/eventos da demo, build reproduzível e budget. **NO-GO:** parser Pawn, AMX vazio, natives stub, execução só de `main()` ou demo sem efeitos de produto.

## Alpha 0.8 — API SA-MP essencial

- **Objetivo/entrega:** perfil proposto em [essential-profile.json](compatibility/essential-profile.json), baseado em SA-MP 0.3.7-R2 com nomes reais checados; escopo pode ser revisado **antes** de implementação, com diff/revisão explícitos. Todos os requisitos desse perfil precisam VERIFIED para fechar o marco. API DL, open.mp e plugins são perfis separados, não misturar versões silenciosamente.
- **Dependências:** C1/S2 e domínio/backend por item. Fonte Pawn, AMX, wire SA-MP e plugin ABI são eixos separados; oferecer API não aceita cliente SA-MP automaticamente. [Política de compatibilidade](compatibility/README.md) é o contrato de promoção de status.
- **Aceitação/testes:** suites de compilação positiva/negativa, binding AMX, parâmetros/defaults/tags/retornos/referências, IDs inválidos, limites e ordem de callbacks; differential/reference tests permitidos com instalação legal e resultados versionados. Stubs que compilam permanecem PENDING. Constantes/limites divergentes do produto precisam de perfil explícito, não falsa igualdade a MAX_PLAYERS=1000.
- **Smoke físico:** gamemode de referência cobrindo itens cliente-dependentes; os puramente servidor podem ser VERIFIED por integração AMX/servidor sem GTA. Relatório publica os itens não cobertos de Dialogs/TextDraws/Actors/Objects/NPC/etc. Nenhum deles vira compatível por pertencer à mesma categoria.
- **GO:** perfil inteiro demonstrado com evidência por item/plataforma. **NO-GO:** compilação como única prova, contagem de declarações confundida com suporte ou promessa de compatibilidade total.

## Alpha 0.9 — modding, assets e ecossistema

- **Objetivo/entrega:** recursos próprios versionados, manifesto com hashes/licenças, download/cache/quota e modelos/texturas demonstrados por adaptador. Primeiro asset precisa ser criado/licenciado para o projeto; nenhum arquivo GTA distribuído. Selecionar formato de recurso e canal de distribuição em decisão própria.
- **Dependências:** M1, entidades/mundo e execução segura, modelo de recursos do servidor; API DL é referência de ergonomia, não obrigação de copiar wire/IDs/formato interno SA-MP. DFF/TXD PC e formatos Android são tratados separadamente.
- **Aceitação/testes:** recurso ausente/corrompido/incompatível, downloads interrompidos, hashes/limites de memória/disco, path traversal, prioridades e troca/remoção/rollback. Mods não substituem binários arbitrários do runtime. Publicar extensão/plugins próprios com ABI/versionamento e superfície permitida.
- **Smoke físico:** dois GTA carregam asset próprio, materializa/atualiza/desaparece a entidade, restart usa cache correto, versão nova/rollback sem asset residual; teste de quota e falha de rede.
- **GO:** pipeline completo e autoria/licença rastreável. **NO-GO:** assets GTA empacotados, cache sem integridade ou carregar código não revisado por conveniência.

## 1.0 — primeira versão estável com escopo publicado

- **Objetivo/entrega:** manifesto de suporte: GTA/loader Windows exatos, modos de conectividade, capacidade/limites, funcionalidades, perfil API, compiler/AMX e plugins/extensões admitidos. Android funcional e cobertura total SA-MP não são requisitos implícitos. Release instalada/reproduzida a partir de fonte e dependências fixadas, com manual e troubleshooting.
- **Dependências:** O1; marcos/capacidades obrigatórios do manifesto 1.0 fechados, perfil essencial VERIFIED, migração/configuração e licenças concluídas. CMake/artefatos recebem versão coerente na tarefa de release autorizada, não neste planejamento.
- **Aceitação/testes:** matriz Windows x86, core/servidor x64/Linux realmente disponíveis; corpus malformado, recursos/VM/redes degradadas, soak mínimo proposto 4 h na capacidade publicada, 100 ciclos cliente e 20 reinícios de servidor sem ghosts/vazamentos/crash atribuível. Reprodução independente do pacote e checksums. Se acesso público entrar no manifesto, autenticação/limites/segurança auditados tornam-se gates obrigatórios; loopback atual não os atende.
- **Smoke físico:** sessão multiplayer completa na topologia/capacidade prometida, criação/remoção/gameplay/Pawn/asset, recovery e encerramento; relatório inclui builds, hardware, participantes e falhas. Nenhum resultado de laboratório substitui esse teste.
- **GO:** escopo estável demonstrado, pendências publicadas fora dele e gate de release separado. **NO-GO:** funcionalidades essenciais só simuladas, incompatibilidades não declaradas ou suporte público/Android/SA-MP total presumido.

## Pós-1.0 — Android, API ampliada e extensões

- **Objetivo/entrega:** cliente Android NDK/JNI próprio no mesmo servidor, perfis adicionais de API clássica/DL/open.mp, NPC, UI/streaming avançados, ferramentas/ecossistema. Cada extensão tem contrato versionado e escopo próprio; não um backlog infinito de “compatível”.
- **Dependências:** A1; versões GTA Android/ABI/loader e assets legalmente identificados, execução/lifecycle homologados em dispositivo real; recursos compartilhados testados entre plataformas. Revisão/toolchain core Android podem ocorrer antes de 1.0 sem prometer cliente funcional.
- **Aceitação/testes:** wire corpus idêntico entre plataformas, endian/float/limites/strings, Android ARM/arquiteturas explicitamente suportadas, pausa/resume/encerramento/recursos, perfis API por capacidade e sem regressão Windows.
- **Smoke físico:** Windows ↔ Android no servidor único, multiplayer/entidades/gameplay/Pawn/asset do perfil publicado, perda de conectividade e ciclo de atividade móvel.
- **GO:** comportamento equivalente demonstrado nos dispositivos/versões declarados. **NO-GO:** simples compilação NDK, offsets PC reaproveitados, emulador como única prova ou perfil SA-MP ampliado sem testes por item.

## Próximos três slices recomendados — entregas finitas

1. **0.3.6 / callback contador real:** entregar módulo experimental opt-in, código específico da solução escolhida e smoke contador. Fechar ownership, instalação concorrente, forwarding e residência com a evidência existente. Não gastar um slice só repetindo LoaderLock/documentação nem criar outro manager genérico. Se gate essencial falhar, artefato fica inerte e bloqueio concreto publicado; não executar experimento sem autorização.
2. **0.3.6 / cena entregue ao processo GTA:** ratificar IPC prioritário observer→ASI e executar spawn/pose/despawn lógicos contra backend simulado no callback homologado. Exigir geração/sessão, limites, budget, falhas de produtor e reset; resolver o mutex/backend antes de engine. Produto demonstrável: comando remoto recebido/drenado no processo correto, sem ped.
3. **0.4 / primeiro remoto 3D:** implementar um backend Hoodlum mínimo, com modelo local fixado e recursos/ABI verificados; conectar a cena/ponte já existentes e cumprir o smoke visual inteiro. Fonte remota sintética é suficiente aqui; dois GTA reais fecham 0.5.

S1/C1 (seleção VM, contratos e testes de assinatura) podem ser trabalhos paralelos aprovados separadamente. Nenhum próximo slice é iniciado automaticamente por este roadmap.
