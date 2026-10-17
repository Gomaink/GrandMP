# Alpha 0.6 / Follow-up 2 — artefato remoto habilitado, sem instalação

Baseline: master, HEAD `eee23777986baebe8849bcd2090b6f298aef6f3e`. Alterações acumuladas Alpha 0.4/0.5/0.6 preservadas. Sem stage/commit. Nenhuma alteração funcional em ASI, ObserverNetwork, Scene, NativeEngine, endereços, hook ou thunk. Diff desta etapa: caso loader_enabled no host existente, registro CTest condicional e este relatório.

## Build executada

```powershell
cmake -S . -B build/alpha06-remote-enabled-win32 -G 'Visual Studio 18 2026' -A Win32 -DBUILD_TESTING=ON -DGRANDMP_BUILD_REMOTE_PED_ENABLED=ON -DGRANDMP_BUILD_PED_SMOKE_ENABLED=OFF -DGRANDMP_BUILD_PED_MOVE_ENABLED=OFF
cmake --build build/alpha06-remote-enabled-win32 --config Debug --target grandmp_remote_ped_enabled -- /nologo /v:minimal
```

Build sem erros ou warnings. Única ASI compilada: `D:/Programacao/Workspace/Applications/grandmp/build/alpha06-remote-enabled-win32/client/windows/remote-ped-enabled/Debug/GrandMPRemotePedExperimental.asi`.

- PE32/i386 DLL, Debug, 599552 bytes.
- SHA-256: `62E6E34FCC25A8E7A318123752F4F6EED2CDB6ECFDEB22ED1E4AF87C6931B134`.
- MAP/PDB no mesmo diretório isolado. Target EXCLUDE_FROM_ALL, opção física OFF por padrão no código CMake. ALL_BUILD não foi executado e não depende desse target.

| Target | Spawn físico | Remoto físico | Situação |
| --- | --- | --- | --- |
| grandmp_remote_ped_enabled | 1 | 1 | Compilado explicitamente |
| grandmp_remote_ped_experimental | 0 | 0 | Preservado, não recompilado |
| grandmp_ped_smoke | 0 | 0 | Preservado, não recompilado |
| targets Alpha 0.4/0.5 habilitados | anteriores | 0 | Opções OFF neste cache; fontes/binários preservados |

## Auditoria do artefato

O script reaproveita os leitores MAP/PE/disassembly da auditoria Alpha 0.5. Cruza endereços MAP com bytes do PE e instruções decodificadas; strings/símbolos isolados não constituem o resultado. Evidência em `build/alpha06-followup2-review/artifact-audit.json` e `artifact-audit-disasm.txt`; dump completo somente da nossa ASI, em pasta ignorada, sem dumps GTA adicionados ao diff.

| Contrato | Evidência no binário (VAs preferidas) |
| --- | --- |
| InitializeASI | Export nominal ligado a _InitializeASI; cdecl, sem argumentos, ret simples. Retém módulo e cria somente worker de preparação. DllMain não cria thread/chama engine. |
| Dois gates abertos | Projeto contém ambos os defines =1. Em prepare, 0x10005334: `33 C0 74 26`, salto tomado para 0x1000535E passa a admissão, pulando a recusa por build. Em runPhase, 0x10004A79 carrega constante 1; o gate remoto não elimina o consumidor. Host real confirma recusa pela identidade, e não pela build. |
| Hoodlum/opt-in/F6 | AdmitExecutable mantém caminhos canônicos estritos, leitura/hash SHA-256 e recognizeGameVersion antes de opt-in/F6/instalação. Digest reconstruído das instruções: A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26. Executável isolado também conferido por leitura. |
| CALL/original/ABI | OriginalCall `E8 FF E2 1F 00`: 0x53C05C → 0x73A360. Inicializador do original mantém esse destino. Thunk conserva flags/GPRs/FX, alinhamento, phaseBody e tail-jump ao original imutável. Nenhum uso de 0x53E981. |
| PIN/publicação/fase | GetModuleHandleExW com flags 5 e verificação do módulo, instalação no thread principal, VirtualProtect/flush/restauração aprovados. runPhase verifica thread/caller 0x53C061, codeMatches e gameplay antes do consumidor. |
| ReceiverState | Startup chama parseLoopbackEndpoint → ObserverNetwork::start → beginRemoteScenePreview. Report armazena estado e desabilita despacho em failed. runPhase compara ready=4 em 0x10004A8E antes de snapshot/tick. |
| Consumidor nativo | Único chamador direto de Controller::tick é runPhase (0x100048E0). Controller chama Experiment; Experiment chama spawnPosition e depois create, ou owns/remove. AddPed cdecl 0x612710 recebe quatro slots, makeWander=false, modelo 7/tipo 4, cleanup de 16 bytes pelo chamador. |
| Cleanup | remove autentica owns antes de CleanUpOldReference thiscall 0x571A00, owned=0/reference=-1 antes de RemovePed cdecl 0x610F20. Nenhum contrato nativo novo. |
| Worker/sink | Worker passa a instância remoteIntents à rede. Vtable efetiva resolve spawn/move/despawn/resetSession do IntentSink. Grafo de chamadas diretas, com esses destinos virtuais explícitos: 902 funções alcançáveis, nenhuma NativeEngine/Controller/Experiment. Outros indiretos de OS/CRT não constituem prova formal de programa inteiro. |
| Posição remota/HELD | IntentSink::move só valida/copía valores sob mutex; não chama NativeEngine. Sem task/setter de coordenadas no caminho remoto e sem supervisor/imports debugger. O símbolo portátil core::teleport é predicado puro de distância (subsd/mulsd, sem calls), não teleporte do GTA. |

Dependências PE esperadas: KERNEL32.dll, USER32.dll, bcrypt.dll, WS2_32.dll, MSVCP140D.dll, VCRUNTIME140D.dll e ucrtbased.dll. Nenhuma dependência inesperada. Runtime Debug MSVC continua necessário.

## Testes executados

Compilados explicitamente no novo cache: grandmp_remote_ped_tests, grandmp_remote_ped_runtime_tests, quatro grandmp_remote_ped_gates, grandmp_ped_smoke_tests, quatro grandmp_ped_build_gates, grandmp_server e grandmp_synthetic_client. Nenhuma outra ASI foi compilada. Comandos completos e logs em `build/alpha06-followup2-review/commands.txt`, `build-enabled.txt` e `build-tests.txt`.

```powershell
ctest --test-dir build/alpha06-remote-enabled-win32 -C Debug -R '^remote_ped_|^ped_smoke_' -E '^ped_smoke_initialization$|^remote_ped_loader$' --output-on-failure -j 1
ctest --test-dir build/alpha06-remote-enabled-win32/reuse-closed -C Debug --output-on-failure -j 1
```

**50/50 + 2/2 = 52/52 PASS**, serialmente. As duas exclusões na primeira seleção são substituídas por receitas descartáveis específicas no subdiretório reuse-closed, que usam os hosts recém-compilados e os ASIs fechados preservados do Follow-up 1. Sem cópia/rebuild de ASIs antigas. Total equivale aos 51 casos anteriores mais loader_enabled.

Abrange F6/F7/F8, join/leave/reentrada/reconexão, registro, geração obsoleta, ownership/identidade, ready/failed, codeMatches, oito combinações de gates e servidor UDP/cliente sintético reais com backend GTA falso. Esses testes não comprovam o gameplay físico.

A **nova ASI habilitada** foi realmente carregada no host descartável (PID 19216): loaded → preparation_read_only → `hoodlum_identity_path_or_live_signature`, status=4, callback=0, sem receiver/hook/spawn. Teste também confirma inicialização duplicada inerte. A recusa é pelo caminho/identidade admitida; esse host não executa a fase de hash contra um GTA em execução e não simula admissão de Hoodlum. Nenhum gate foi enfraquecido.

## Preservação e instalação antiga

Os **60 ASIs exigidos**, mais os dois fechados gerados no Follow-up 1 (**62 no total**), mantiveram SHA-256 e timestamps de modificação. Todos os fontes funcionais, headers nativos/hook/thunk, manifestos anteriores, index e HEAD preservados. GrandMP.asi instalado: `1EB0E9E22591402FA6002363DD0FC22B873B27F8224EA11C1FA3B356EEC7D2FA`.

Somente leitura: ACTIVE.txt continua apontando para smoke-b-install-20261017-132401-053; ASI Alpha 0.5 instalada mantém SHA `99A5B79CA037BE777F8A508DD7AB99BE1D6859A1B9772AABBFDDEC200EE30E67`; contador arquivado mantém SHA `AC0B839D5E524DDAD659588EC9B9894C2B0EDC1A07FEA8EB57DB23E2FDCC846C`. Detalhes em installation-readonly.json/manifest.json. Não foi restaurado/removido/substituído nada em scripts, backups ou saves. git diff --check aprovado, sem stage/commit/push/merge.

## Decisão

**GO para PREPARAR o smoke físico**, sujeito à revisão deste artefato. Instalação, restauração e execução exigem autorização separada; nada disso foi realizado. Preparação deve tratar a instalação Alpha 0.5 ainda ativa: as duas ASIs usam o mesmo CALL e não podem coexistir.

Riscos mantidos: primeiro remoto físico ainda não demonstrado; spawn depende de primeira posição lógica válida e geometria/gameplay seguros perto de CJ; falha é one-shot, sem respawn automático; perda de código/ownership pode deixar entidade sem cleanup autorizado; leave UDP pode depender de snapshot/timeout; UDP v1 não possui epoch do receptor e IDs aposentados são recusados (histórico 64); um remoto, sem movimento físico. Futuro smoke: F6 arma → cliente sintético entra/envia posição → confirmar um PED → saída graciosa → confirmar remoção da mesma entidade; encerrar sem salvar e preservar log/evidência visual.
