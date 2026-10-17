# Alpha 0.6 / Slice 0 — um remoto, aguardando revisão física

Baseline `master/eee2377`, Alpha 0.4/0.5 ainda sem commit. Closeout Alpha 0.5 separado em `build/alpha06-remote-ped-review/ALPHA05-CLOSEOUT.md` e `alpha05-closeout.json`: hashes e logs por PID 7172/PED 172A99E8 (chegada/parada/remoção) e 5388/PED 19A98A60 (F8 durante caminhada). A mensagem `move_failed/ped_lifecycle_ended_or_removal` do Smoke B encerra a observação; não é evidência de falha física.

**Restauração pendente:** leitura encontrou `build/alpha05-smoke-sessions/ACTIVE.txt`, `scripts/GrandMPPedSmoke.asi` SHA `99A5B79CA037BE777F8A508DD7AB99BE1D6859A1B9772AABBFDDEC200EE30E67` instalada e o counter arquivado em `smoke-b-install-20261017-132401-053`. Não foi restaurado nada automaticamente. Resolver essa pendência com o operador antes de preparar qualquer instalação nova.

## Arquitetura encontrada e ligação implementada

Servidor UDP v1 (`server/src/main.cpp`/SessionTable) emite RemoteJoin/RemotePosition/RemoteLeave e snapshots, exclusivamente em loopback. Cliente sintético é outro peer real. ObserverNetwork valida sender/nonce/protocolo, mantém RemoteRoster e chama RemotePlayerScene. **RemotePlayerScene só emite spawn depois da primeira posição válida**, não no Hello/RemoteJoin isolado. Seu sink recebe spawn/move/despawn/resetSession. `grandmp_remote_scene_host` existente é executável externo com backend simulado; não estava conectado à ASI.

A nova ASI reutiliza **ObserverNetwork dentro do processo GTA**, em worker próprio. Não há IPC nem protocolo novo. Fluxo efetivo:

```text
servidor UDP v1 + cliente sintético (127.0.0.1)
  -> worker ObserverNetwork -> RemoteRoster -> RemotePlayerScene
  -> IntentSink (um snapshot de valores sob mutex, sem ponteiros nativos)
  -> callback GTA 0x53C05C -> Controller -> Experiment/NativeEngine aprovados
```

O snapshot é copiado e o mutex liberado antes de qualquer operação do motor. Worker recebe/valida/publica, nunca consulta/cria/remove PED. O consumidor só roda após as verificações existentes de caller/thread/gameplay/identidade. Streaming, posição segura perto de CJ, AddPed(false), pool/handle/referência registrada e RemovePed são os mesmos da Alpha 0.5; nenhum offset/ABI novo.

O QueuedRemotePlayerAdapter existente foi preservado: seu contrato exige create atômico sem entidade parcial e admite retries. O lifecycle nativo leva vários callbacks e pode reter resultado de identidade desconhecida; por isso este slice usa um sink específico de um slot e o Experiment one-shot existente, sem modificar o adapter genérico.

## Identidade e lifecycle

- Chave remota `{generation, playerId}` independente do ponteiro/handle nativo. Revisão ordena snapshots; geração muda na admissão/boundary. Snapshot antigo, incluindo leave, não substitui intenção atual.
- Primeiro spawn válido ocupa o único slot. Duplicata não aloca novamente; segundo remoto é ignorado, sem promoção automática por posições após saída do primeiro. É necessário novo spawn lógico/sessão para admissão posterior.
- Leave deve casar com o ID selecionado. Antes da primeira fase cancela sem engine calls; durante streaming cancela antes do AddPed; depois remove apenas o PED cujo ownership nativo continua provado.
- Reconexão com nova identidade/geração faz cleanup confirmado antes de nova criação, em outro callback. Falha não é retentada na mesma geração. Perda de ownership ou falha com PED não nulo bloqueia todo o backend remoto até reiniciar o processo; não se tenta remover/substituir resultado desconhecido.
- UDP v1 não inclui epoch do receptor em RemoteLeave/RemotePosition. Intenções já publicadas são protegidas pela geração local; IDs anteriormente encerrados são recusados inclusive após reset, evitando aceitar reuso ambíguo. Histórico fixo de 64 IDs; esgotamento bloqueia novas admissões. Reuso legítimo de ID exige reiniciar o experimento. Isso não é autenticação de rede nem garantia contra todo replay de datagrama v1 nunca observado antes; smoke limitado ao servidor local controlado, sem reiniciá-lo no meio da sessão.
- Posições recebidas são validadas/armazenadas para diagnóstico. **Não são usadas como coordenadas do PED**, nem geram tarefas/teleportes/updates físicos. Spawn ocorre na posição segura já aprovada perto de CJ.

## Isolamento e gates

| Variante | Comportamento | Estado nesta entrega |
| --- | --- | --- |
| GrandMPPedSmoke e targets Alpha 0.4/0.5 | F6/F7/F8 originais | Preservados; binários homologados intactos |
| GrandMPRemotePedExperimental | F6 arma a fase; rede governa spawn/leave; F7/F8 locais não comandam remoto | Spawn físico e gate remoto fechados |

**Não coinstalar as duas ASIs experimentais**: ambas usam o mesmo CALL e a admissão exige original conhecido. Na variante remota não há PED manual F6 nem controle F7/F8 remoto; o fluxo homologado permanece nos targets antigos. Assim nenhum leave de rede chega ao registro de entidade manual. Os registros C++ são distintos e há teste de isolamento. `GrandMP.asi` não foi modificado.

Target `grandmp_remote_ped_experimental` compila `ped_smoke.cpp` com `GRANDMP_REMOTE_PED_VARIANT=1`, observer existente e `.def` próprio. O ramo sem esse define foi comparado com a baseline e tem tokens equivalentes. Sem refatorar NativeEngine ou seu writer/thunk. Output: `build/alpha06-remote-ped-win32/client/windows/remote-ped/Debug/GrandMPRemotePedExperimental.asi`; log adjacente `GrandMPRemotePedExperimental.log`. Exports diagnósticos herdados mantêm nomes GrandMPPedSmokeStatus/CallbackCount; não identificam outro módulo.

Ativação física futura exige **ambos** `GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1` e `GRANDMP_REMOTE_PED_PHYSICAL_BUILD=1`; nenhum é definido neste target. Não existe target habilitado Alpha 0.6 nesta entrega. Variável de ambiente não abre build gate. Futuramente, identidade Hoodlum/path estritos, PIN, opt-in existente `GRANDMP_PED_SMOKE_ENABLE=1`, foreground/F6 e endpoint explícito `GRANDMP_REMOTE_PED_SERVER=127.0.0.1:7777` continuam obrigatórios. Worker só começa depois de instalação/PIN; falha de endpoint não cria entidade. Thread de rede vive até o processo terminar, sob PIN, sem rotina de unload/recuperação automática.

Logs de fase/lifecycle existentes permanecem, com `remote=<id> generation=<g>` nos eventos nativos do novo engine. `remote_join_intent_position_not_applied` distingue admissão de representação física; só `ped_created` confirma retorno/validação nativa, não visibilidade na tela. Ownership perdido é terminal, sem respawn. Modelo/solo/colisão ainda podem recusar a criação.

## Verificação desta entrega

Build Debug/Win32, VS 18 2026, novo diretório. Compilados os targets remoto/testes, padrão bloqueado/testes para regressão, servidor e sintético. Uma advertência inicial de nome LIBRARY foi corrigida com `.def` exclusivo e diretórios LIBRARY/RUNTIME/ARCHIVE isolados; build final remoto sem warnings. O binário intermediário do primeiro link permanece apenas no cache `client/windows/Debug`, não é artefato para instalação. Hashes de todos os artefatos novos/preservados em `build/alpha06-remote-ped-review/handoff.json`.

CTest serial: **38/38 passaram** (24 regressões + 12 casos remotos + loader bloqueado + UDP loopback real). Join/duplicata/capacidade/leave/cancelamento/reconnect/stale/ownership/spawn failure/thread inválida/isolation/reentrância cobertos. O consumidor também foi testado com ID numérico igual em geração nova e leave carimbado antigo; o produtor v1 real recusa conservadoramente esse reuso. Loader descartável confirma DllMain inerte, status bloqueado, zero callbacks mesmo com variáveis de opt-in. Teste loopback usa servidor e sintético reais, ObserverNetwork/Scene/IntentSink/Controller de produção e engine falso com afinidade de thread. Não comprova gameplay físico nem executa natives GTA. Testes de F7 permanecem como regressão, sem movimentação remota.

```powershell
cmake -S . -B build/alpha06-remote-ped-win32 -G 'Visual Studio 18 2026' -A Win32 -DBUILD_TESTING=ON -DGRANDMP_BUILD_PED_MOVE_ENABLED=OFF -DGRANDMP_BUILD_PED_SMOKE_ENABLED=OFF
cmake --build build/alpha06-remote-ped-win32 --config Debug --target grandmp_remote_ped_experimental grandmp_remote_ped_tests grandmp_ped_smoke grandmp_ped_smoke_tests grandmp_server grandmp_synthetic_client
ctest --test-dir build/alpha06-remote-ped-win32 -C Debug -R '^remote_ped_|^ped_smoke_' -E '^ped_smoke_build_gates_' --output-on-failure -j 1
git diff --check
```

Closeout está separado do patch `build/alpha06-remote-ped-review/alpha06-incremental.patch`. Não incluir binários/dumps/arquivos proprietários no commit futuro. Nenhum stage/commit, GTA, instalação, hook físico, alteração de saves/servidor/protocolo/supervisor HELD nesta tarefa.

## Futuro smoke, somente após revisão/autorização

1. Resolver a restauração pendente com GTA fechado e conferir hashes. Revisar esta integração e preparar/auditar futuramente uma variante remota explicitamente habilitada; não instalar a build fechada desta entrega. Não coinstalar PedSmoke.
2. Iniciar servidor existente em 127.0.0.1:7777. Configurar os dois opt-ins indicados e iniciar GTA isolado manualmente após autorização. Em gameplay exterior aberto, F6 apenas arma o hook/receiver.
3. Iniciar **um** `grandmp_synthetic_client --port 7777`. Confirmar no log remote ID/geração, model_requested/model_loaded/ped_created e CJ + um PED visível perto dele. Posições sintéticas não podem mover esse PED. Comparar identidade registrada nos eventos.
4. Encerrar o sintético graciosamente (Ctrl+C). Confirmar `removed`, mesma identidade/handle e desaparecimento somente do remoto, sem respawn. Caso de perda UDP pode depender de snapshot/expiração (8–10 s).
5. Reconectar sintético no mesmo servidor, com novo ID; confirmar geração nova e cleanup anterior. Não reiniciar servidor durante o smoke. Falha/ownership perdido encerra a tentativa, sem remover identidade desconhecida.
6. Encerrar GTA sem salvar; preservar log, hashes, PID e evidência visual. Só então alterar/restaurar arquivos, mediante autorização. F8 do experimento local não é comando de remoção remoto.

Estado: código/build/testes prontos para revisão; **NO-GO físico nesta entrega**. Permanecem pendentes revisão do pequeno ramo nativo, preparação do artefato habilitado, saneamento da instalação e primeiro smoke de join/leave no motor real.
