# Alpha 0.3.6 / Slice 3 — Native Game Thread Integration

## Decisão

**NO-GO para instalar automaticamente em InitializeASI ou executar smoke de callback com os artefatos atuais.** A hipótese de instalação antecipada não fornece exclusão demonstrada dos executores. Não foi criado um instalador, wrapper GTA habilitado, callback ou patch real nesta slice.

O candidato prioritário passa a ser **instalação externa supervisionada durante um evento de depuração mantido pendente**. Ela oferece uma barreira concreta para a escrita do CALL, descrita abaixo. Ainda faltam seu writer transacional específico, a preparação/residência admitida pelo debugger e um wrapper x86 auditado. Esses componentes precisam ser testados no host contador existente antes de autorizar GTA. Não há novo host genérico nem proposta de repetir o diagnóstico de LoaderLock como entrega.

## Evidência reutilizada e investigação nova

Os logs originais foram somente lidos; nenhuma nova sessão GTA foi executada.

| Evidência | Conclusão e limite |
|---|---|
| `20261012-223706`: InitializeASI e CALL na TID `0x49B8`, PID `0x5694` | Mesmo thread nessas ocorrências; não prova exclusividade nem todas as fases do motor. Primeira captura de CALL não comprova menu. |
| `20261013-002726`: PID `0x4C80`, TID `0x5B04`, LoaderLock livre por PDB exato | Snapshot distinto; não generalizar a todos os triggers/configurações. A medição permanece encerrada. |
| Ambas as entradas InitializeASI listam quatro threads | Refuta a premissa de instalação antes de existirem outras threads. Não prova que elas executem o CALL nem que sejam incapazes de fazê-lo. |
| Retornos GTA `0x820F97`, `0x81751F`, `0x748806` nas duas stacks | Frames GTA/UAL têm aviso de unwind sem símbolos confiáveis. Imports e instruções do arquivo homologado corroboram a associação funcional abaixo; não usar nomes de exports próximos como nomes reais dos frames. |

Nova investigação read-only com `dumpbin` 14.51.36260 e parsing dos headers/imports PE confirmou o trigger observado:

| VA / offset de arquivo | Bytes relevantes / interpretação |
|---|---|
| `0x748801` / `0x347C01` | `E8 1D ED 0C 00`: CALL ao thunk `0x817523`; retorno `0x748806`. |
| `0x817508` | Thunk com IAT `0x8E3EC4`, delay-import `DINPUT8.dll!DirectInput8Create`. |
| `0x81751A` / `0x41691A` | `E8 9E 98 00 00`: CALL ao helper `0x820DBD`; retorno `0x81751F`. |
| `0x820F91` / `0x420391` | `FF 15 6C 80 85 00`: CALL através de IAT `0x85806C`, identificada como `KERNEL32!GetProcAddress`. |
| `0x53E981` / `0x13DD81` | `E8 5A D5 FF FF`: CALL completo para `0x53BEE0`. |

Portanto, o caminho observado é a resolução tardia de `DirectInput8Create` acionando `GetProcAddress`, coerente com `CustomGetProcAddress` do loader; não é uma prova de carga antes de WinMain por `GetStartupInfoA`. O trecho de inicialização anterior já contém chamadas a `0x530500` e `0x530640`. A fonte do loader não estabelece uma barreira para impedir outros executores do CALL durante a inscrição.

Proveniência fixada:

- GTA local: SHA-256 `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`, PE32/i386, base `0x00400000`, 14.383.616 bytes.
- Loader local: SHA-256 `29FEE0AA46414864AD1F65B2689B470BFB2CF2F09A94E2AFD271E68F2E16743B`, Win32 7.7.0.
- [UAL 7.7.0, commit `38c04fdf7e6b1e7fb9c570204600d13c9a91bf76`](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/38c04fdf7e6b1e7fb9c570204600d13c9a91bf76/source/dllmain.cpp): `FindFiles` chama a exportação sincronicamente após LoadLibrary; `LoadPluginsAndRestoreIAT` seleciona/restaura o trigger e chama carga única; `CustomGetProcAddress` o invoca. Nenhum desses passos exclui executores do CALL. Licença MIT, sem código incorporado.
- Atenção: `build/ual-reference` está em `71ca8ae020b0be3ff6daec4739e105b51410070f` e **não** representa 7.7.0. Nesta investigação foi usada a fonte oficial fixa acima. Hash do conteúdo UTF-8 consultado de `source/dllmain.cpp`: `6E81CD0A93641E4090A730BF3EAA74D8ED422C8643FF007E52562B2BC83787BE`.
- [plugin-sdk `15f15b60bbf74c106e1b496ff92c98764abf4605`](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/shared/Events.h): `gameProcessEvent`, `void __cdecl()`, AFTER, H_CALL `0x53E981`; checkout local confirmado. Auditoria de `EventList.h`/injector preservada em [native-counter-prototype.md](native-counter-prototype.md). Sem dependência incorporada.

Saída bruta read-only, imports, ranges e hashes foram salvos em `build/alpha036-win32/slice3-static-evidence.txt`, ignorado pelo Git. Para reproduzir, usar o dumpbin do MSVC com `/DISASM /RANGE:<inicio>,<fim>` nos ranges `0x820F50,0x820FB0`, `0x8174E0,0x817550`, `0x7487C7,0x748818`, `0x53E981,0x53E9B0` e `/IMPORTS` sobre o arquivo homologado. Começos/fins de ranges podem cortar instruções; só as instruções completas corroboradas acima fundamentam a conclusão. O algoritmo PE usa RVA = VA − ImageBase e offset = PointerToRawData + RVA − VirtualAddress da seção correspondente. Nenhum executável/asset foi copiado para o repositório.

## Respostas aos gates de integração

| Questão | Resultado |
|---|---|
| InitializeASI cedo em todos os caminhos homologados? | **Não demonstrado.** Carga ocorre no trigger importado; `LoadFromAPI`, ordem de plugins e chamadores podem alterar o caminho. Duas ocorrências não cobrem todos os caminhos. |
| Thread de inicialização e CGame::Process? | Mesma TID `0x49B8` no snapshot da sessão `0x5694`. Sem prova de todos os dispatches; wrapper futuro registrará divergência de thread fora do contador. |
| Ausência de execução durante escrita? | **Ausente no caminho ASI/plugin-sdk.** PIN, bytes, mutex local e FlushInstructionCache não criam exclusão. Evento Windows pendente oferece exclusão para o candidato externo. |
| Recusar patch de terceiros? | Conferir os cinco bytes e destino dentro da mesma parada completa, antes de escrever. Divergência recusa; não encadear ou sobrescrever patch desconhecido. |
| Original e ABI? | Call-site sem argumentos, assinatura publicada cdecl void e contexto pós-CALL são coerentes. Após retorno, ECX é redefinido em `0x53E986`, há outra chamada e flags do salto posterior vêm de `test`. O wrapper compilado e preservação ESP/registradores ainda precisam de testes; não existe wrapper GTA validado. |
| Residência? | PIN do próprio módulo validado por endereço fora de DllMain, antes de READY/publicação; irreversível até exit. Laboratório testa esse mecanismo, mas o ASI GTA atual permanece inerte e sem PIN. |
| Duplicação/concorrência GrandMP? | Reutilizar bootstrap atômico existente; um único controlador proprietário do evento e uma única publicação. Tokens/estado local só coordenam GrandMP; não substituem exclusão Windows. |
| Garantia contra terceiros? | A parada bloqueia threads do alvo; não impede outro processo de escrever memória nem adulteração posterior após retomar. Primeiro smoke exige ambiente cooperativo e somente plugins conhecidos. Não se promete proteção universal. |

## Candidato concreto: transação externa sob parada completa

A [Microsoft documenta os eventos de depuração](https://learn.microsoft.com/en-us/windows/win32/debug/debugging-events): quando há evento para o debugger, todas as threads do processo afetado permanecem suspensas até a continuação desse evento; criação de thread é notificada antes da execução em modo usuário. Isso oferece exclusão durante uma transação externa, sem depender de enumerar e suspender arbitrariamente threads em execução no ASI.

Plano mínimo, **ainda não implementado/aprovado para smoke**:

1. ASI experimental separado continua inerte por padrão. O debugger pode admitir somente preparação leve/residência numa entrada verificada, sem engine. É necessário definir/testar esse handshake antes de liberar PIN; não presumir que o snapshot histórico autorize qualquer chamada futura.
2. Após residência e estado imutável preparados, um marcador READY exportado é atingido por breakpoint de hardware. Não criar sockets, workers ou timers no GTA. A parada READY não é prova de thread-affinity do motor; ela só delimita preparação.
3. Um único controlador externo conserva o evento pendente. Verifica PID/identidade/base, módulo e endereço do wrapper, pin/readiness, loader/configuração/plugins e contextos de threads. Aborta se algum EIP estiver no interior da região modificada ou se o estado de instalação for ambíguo.
4. Sob a mesma parada, valida CALL/destino originais; usa VirtualProtectEx, WriteProcessMemory de exatamente cinco bytes, readback/decode do destino, FlushInstructionCache e restauração da proteção. Só continua após confirmação de todas as operações. Comparações de bytes passam a estar protegidas da execução concorrente do alvo pela parada Windows.
5. Qualquer falha parcial exige restaurar e confirmar os bytes/proteção/cache ainda com o evento pendente. Se não puder comprovar instruções íntegras, não continuar nem destacar: preservar evidências e oferecer término controlado do processo experimental. Nunca entregar instrução parcial ao jogo.
6. Wrapper x86 encaminha a original `0x53BEE0` exatamente uma vez e depois chama o contador lock-free. Preservar ESP e registradores requeridos pelo contrato e auditar a saída do compilador. Contador executa somente `fetch_add`; gate/desativação e leitura diagnóstica ficam fora dele. Não há trampoline de prólogo nem uso de `Events::Add/Remove`.
7. Tratamento de STATUS_SINGLE_STEP limita-se ao breakpoint identificado. Limpar DR/breakpoints e destacar com fluxo `/h` validado; não mascarar exceções reais. O patch, wrapper e estado permanecem residentes até exit, sem restauração em DllMain ou hot-unload.

**Não usar `.call` ou instalar de dentro do processo parado.** Retomar código do alvo para instalar deixa de ser uma transação externa sob parada completa e pode aguardar locks de outras threads paradas. Detours/MinHook sem resolver suas falhas já auditadas também não substituem essa barreira. CDB e seus probes de hardware/detach podem ser reutilizados, mas a infraestrutura atual de captura passiva não é um writer transacional.

O menor trabalho técnico seguinte, sujeito a autorização, é implementar/testar **essa única transação para esse único CALL** e o handshake/wrapper no host contador existente, incluindo falhas de escrita/proteção/cache/retomada. Não criar um novo gerenciador ou laboratório genérico. Sem esses testes e revisão, o artefato GTA deve continuar sem instalador.

## Smoke futuro, condicionado e reversível

Este roteiro não autoriza executar o jogo nem instalar artefatos agora. Após os gates acima:

1. Com `D:\Jogos\GrandMP-Smoke` fechado, usuário instala manualmente apenas o experimental aprovado/configuração opt-in; reconfirma hashes, loader 7.7.0, Windowed Mode desativado e lista conhecida de plugins.
2. Usuário inicia a sessão supervisionada. Preparação admitida e parada READY precedem qualquer escrita. Fingerprint/bytes/módulo/contexto divergentes, falta de residência ou erro no controlador abortam antes da continuação.
3. Depois da transação confirmada, observar pela primeira vez o contador nativo e o thread do wrapper em menu visualmente confirmado, loading e partida. Verificar original exactly-once, sem exceções, depois encerrar normalmente. Uma ocorrência não fecha os gates de budget/soak/10 ciclos do roadmap.
4. Diagnóstico: preservar logs de validações, transação, contador/thread, dump/erro se houver. Instalação indeterminada não recebe detach/resume; restauração comprovada ocorre só na parada proprietária, ou encerra-se o processo. Após a retomada não tentar hot-unload/Remove/restore automático.
5. Rollback de artefatos somente com processo encerrado: desabilitar opt-in e retirar experimental/configuração; manter GrandMP.asi homologado e loader intactos. O executável em disco nunca recebe patch.

A evidência nova esperada é o **callback contador real após a rotina original**, instalado sob exclusão documentada. Não é outra medição de TID/LoaderLock, nem prova de disponibilidade de ped. O próximo gate para Alpha 0.4 continua sendo contexto/ABI de criação e destruição de entidades depois de N1/N2 homologados.

## Fechamento das Slices 1/2 e proposta de commits

Baseline preservada: `af3af1c90c61216dfc153a2f51b14f2368e2c8bb`. Índice vazio; nenhuma operação de staging/commit foi executada. Novos arquivos, diffs e whitespace foram revisados.

Correções concretas durante o fechamento, a incluir na Slice 2:

- Dois spawns durante remove in-flight: teste `reuse_during_remove` ampliado reproduziu falha antes da correção. Coalescing agora preserva create pendente da nova representação.
- Welcome + snapshot no mesmo tick: teste do pipeline reproduziu perda permanente de spawn causada por reset depois de tick. `IRemotePlayerSceneSink::resetSession()` default preserva sinks existentes; ObserverNetwork o notifica antes de sincronizar a nova cena; adapter abre geração naquele momento. Host só registra a mudança, sem reset posterior.
- Backend do host honra criação idempotente por ID; cleanup usa o mesmo tipo de logging wide do restante da saída.
- Harness UDP encerra Winsock por RAII somente após destruir sockets, inclusive nos retornos de falha.

Separação proposta, sem aplicar staging:

| Commit | Conteúdo |
|---|---|
| `feat(windows): add inert native counter prototype` — Slice 1, 8 paths | `native_counter_experimental.cpp`, `.def`, `native_counter_gate.hpp`, `native_counter_host.cpp`, `native_counter_lab.def` em `client/windows/tools/`; `docs/native-counter-prototype.md`; apenas hunk final dos targets counter em `client/windows/CMakeLists.txt` e hunk final dos testes counter em `tests/CMakeLists.txt`. |
| `feat(client): bridge replicated scene to owner-thread adapter` — Slice 2, 13 paths | ObserverNetwork header/cpp; adapter header/cpp; scene sink header; testes adapter e `observer_adapter_pipeline_tests.cpp`, `remote_scene_loopback.py`; host `remote_scene_preview_host.cpp`; `docs/native-scene-bridge.md`, alteração de `docs/ROADMAP.md`; demais hunks nos dois CMakeLists. Inclui as correções de fechamento. |

Os dois CMakeLists são compartilhados entre commits e exigem seleção por hunks; não adicioná-los integralmente no primeiro commit. Verificar/testar cada estado antes do commit quando autorizado. Este relatório pertence ao trabalho da Slice 3 e deve ser versionado separadamente. Build, logs, checkouts de pesquisa e material GTA ficam fora de ambos.

## Validação e pareceres

Build Debug Visual Studio 18 2026 / MSVC 19.51.36260 e regressão completos foram reexecutados após todas as correções: **122/122 Win32** (12,87 s) e **110/110 x64** (6,54 s), exit code 0 nas duas arquiteturas. Os cenários existentes foram ampliados; a contagem não aumentou. O loopback com servidor e dois clientes sintéticos reais passou novamente. Logs finais: `build/alpha036-win32/slice3-build.txt`, `slice3-ctest.txt` e os correspondentes em `build/alpha036-x64`. Os testes de regressão novos falharam deliberadamente na implementação anterior e passaram após os ajustes; relatórios antigos de falha não substituem os logs finais.

```powershell
cmake --build build/alpha036-win32 --config Debug --parallel 4
ctest --test-dir build/alpha036-win32 -C Debug --output-on-failure
cmake --build build/alpha036-x64 --config Debug --parallel 4
ctest --test-dir build/alpha036-x64 -C Debug --output-on-failure
```

Os diretórios reutilizam configurações aprovadas com o gerador `Visual Studio 18 2026`, `-A Win32` e `-A x64`. `git diff --check` passou; a conferência dos arquivos não rastreados não encontrou whitespace final. Índice permaneceu vazio. Fontes/builds do ASI homologado não receberam alteração de código; nenhum artefato foi copiado para o jogo.

- **Architect:** identificou a corrida de segundo spawn durante remove; aprova separação das camadas e propõe all-stop externo para o experimento. Sem prova de instalação antecipada, NO-GO GTA.
- **Windows native:** confirmou imports/thunks da nova evidência, revisão correta do UAL e assinatura/contexto do CALL; concluiu NO-GO InitializeASI antecipado e recomenda transação externa, sem `.call`.
- **Reviewer read-only:** encontrou/percorreu a correção da fronteira Welcome+snapshot e verificou o cleanup Winsock; sem outros bloqueadores nos componentes isolados. Aprova especificação condicionada, mantém NO-GO físico porque handshake READY, writer e wrapper ainda não existem. Não executou testes; os resultados acima são do agente principal.

O sucesso dos testes continua limitado ao host/ponte simulada; o experimental GTA permanece inerte. A Alpha 0.3.6 não está concluída nem homologada para motor, callback real ou entidades.
