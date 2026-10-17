# Alpha 0.6 / Follow-up 1 — revisão final, sem execução física

HEAD `eee23777986baebe8849bcd2090b6f298aef6f3e`, master, alterações anteriores preservadas e sem stage/commit.

## Registro nativo (implementação preservada)

`NativeEngine::remove` continua idêntico ao início deste follow-up:

1. `owns(ped)` demonstra ponteiro exato, diferente de CJ, pool/slot/tag vivos, resolução do handle e referência registrada. Falha retorna sem cleanup.
2. Guarda `victim = owned`; `CleanUpOldReference` (0x571A00, thiscall) desvincula `&owned` da lista da entidade ainda autenticada. Não se presume que essa chamada destrua o PED ou zere o armazenamento.
3. Zera explicitamente `owned` e atribui `reference = -1`, **antes** de `RemovePed`.
4. `RemovePed` (0x610F20, cdecl) remove do World e chama destrutor virtual, conforme evidência Hoodlum já registrada em `ped-smoke-alpha040.md`. Não há leitura posterior de victim.

Após retorno normal, o registro fica vazio. `creationResult` conserva apenas o endereço bruto diagnóstico, sem autorizar desreferência. O Controller encerra esse callback de cleanup antes de qualquer substituição. Na nova geração, `beginRemote` exige `owned == nullptr && reference == -1`, troca a chave e zera creationResult. O endereço do armazenamento registrado permanece estável.

Se o motor invalidar uma referência durante destruição externa, `owned` pode virar nulo sem liberar nosso handle. Isso não equivale ao cleanup confirmado: beginRemote recusa o registro e ownership perdido envenena o backend. O teste simula essa possibilidade; não atribui essa causa a um smoke anterior.

`remote_ped_registry` modela referência vinculada a armazenamento estável, slot/tag do pool, unlink, liberação anterior à destruição e reutilização do endereço com tag nova: A criado → removido → registro vazio → B admitido. Também verifica invalidação externa sem remoção/reutilização. É modelo mais fiel, **não execução do destrutor GTA**.

## Inicialização e falha de código

Logs específicos: `remote_network_thread_created_not_receiver_ready`, `remote_network_thread_entered`, `remote_endpoint_validated_loopback`, `remote_observer_initialized`, `remote_receiver_ready_no_engine_calls`. As linhas da thread criadora e worker podem intercalar. Ready significa receptor local apto a tick, não Welcome/conexão confirmada.

Falhas distintas: `remote_network_thread_create_failed_win32=N` (erro capturado imediatamente), `remote_receiver_failed_endpoint_invalid`, `remote_receiver_failed_observer_start`, `remote_receiver_failed_preview_start`, `remote_receiver_failed_initialization_exception`. Sem restart: falha de inicialização marca receiver failed, desabilita despacho, cancela intenções, fecha rede e termina worker. Nenhuma operação de engine ocorre no worker. Controller só recebe intents após ready.

O ramo remoto de runPhase agora valida código mesmo fora de gameplay. Falha liga runtimeDisabled e encerra **o Controller remoto**, sem usar experiment F6. Log único `live_code_changed_remote_terminal_no_cleanup`; não consulta ownership, não limpa/remove PED, não reativa ao restaurar bytes. O endereço já registrado permanece diagnóstico; reinício do processo encerra a tentativa. O thunk continua encaminhando o original aprovado; nenhuma chamada adicional de gameplay é autorizada pelo ramo de falha.

## Gates e targets

| Target | Spawn físico | Remoto físico | Movimento físico |
| --- | --- | --- | --- |
| grandmp_ped_smoke | 0 | 0 | 0 |
| grandmp_ped_smoke_enabled | 1 | 0 | 0 |
| grandmp_ped_smoke_move_enabled | 1 | 0 | 1 |
| grandmp_remote_ped_experimental | 0 | 0 | sem comandos |
| grandmp_remote_ped_enabled (futuro) | 1 | 1 | sem comandos |

`GRANDMP_BUILD_REMOTE_PED_ENABLED` é OFF por padrão. Target futuro só existe com opção ON, usa EXCLUDE_FROM_ALL e saída `client/windows/remote-ped-enabled/Debug`. Exige simultaneamente `GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1` e `GRANDMP_REMOTE_PED_PHYSICAL_BUILD=1`. Nenhuma variável de ambiente abre os gates. Os alvos antigos e o alvo remoto fechado não recebem esses defines.

## Validação executada

VS 18 2026, Debug/Win32, cache novo `build/alpha06-followup1-win32`, BUILD_TESTING=ON, todas as opções físicas OFF. Compilados somente remoto fechado, padrão fechado, testes/gates e server/synthetic do loopback. Uma declaração duplicada foi detectada na primeira compilação e removida; build final terminou com sucesso.

`ctest --test-dir build/alpha06-followup1-win32 -C Debug -R '^remote_ped_|^ped_smoke_' --output-on-failure -j 1`: **51/51**, 7,02 s. São 24 regressões F6/F7/F8, 15 unidades remotas (12 anteriores + registry/receiver/code), 2 ramos reais em host descartável, 1 loader fechado, 1 loopback real com engine fake e 8 testes das matrizes de gates.

Host descartável inclui a implementação real fechada: thread criada com endpoint inválido termina em failed, sem callback/hook; runPhase com perfil de código não demonstrável termina remoto antes das consultas de gameplay. Um teste com entidade fake já criada confirma ausência de qualquer engine call após disable. O host não modifica bytes do GTA nem comprova o gameplay físico.

Cache separado `build/alpha06-followup1-gates-win32`, opção remota ON, foi **somente configurado**: projeto contém ambos os defines e saída própria, não é dependência de ALL_BUILD, nenhum ASI foi gerado ali. Target habilitado não foi compilado.

Artefato novo **fechado**: `build/alpha06-followup1-win32/client/windows/remote-ped/Debug/GrandMPRemotePedExperimental.asi`, 599040 bytes, SHA-256 `E3F6E114D99803423EBD3EAF4DF22D4CE31ECCF80A66D9490E7370EF86A77CF0`.

Os 60 ASIs anteriores (57 do manifesto original + 3 do Slice 0), SHA-256 e mtimes, permanecem iguais. Hoodlum mantém SHA `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`. Index Git, headers nativos, NativeEngine, hook/thunk, streaming, PIN e caminhos F6/F7/F8 preservados (incluindo equivalência dos tokens legados sem GRANDMP_REMOTE_PED_VARIANT). Manifesto, log CTest e patch incremental estão em `build/alpha06-followup1-review/`.

## Decisão e pendências

**GO para compilar futuramente o target remoto habilitado, após revisão deste follow-up; sem autorização de instalação/smoke.** Não houve GTA, instalação/cópia de ASI, hook físico, alteração de saves/servidor/protocolo, restauração ou supervisor HELD.

Riscos mantidos: primeiro PED remoto ainda sem validação física; testes de lifecycle são modelos; falha de código/ownership pode deixar entidade sem cleanup autorizado; gates não garantem visibilidade física; IDs v1 aposentados não podem ser reutilizados no processo (histórico limitado a 64); somente um remoto, sem movimentos; ASIs locais e remota não podem coexistir no mesmo CALL. ACTIVE.txt e ASI Alpha 0.5 instalada continuam pendentes de restauração autorizada, sem alteração neste trabalho.
