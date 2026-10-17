# Alpha 0.3.6 — dry-run de admissibilidade

Esta slice integra o preflight existente, READY/PIN, wrapper x86 e o planejamento do writer de laboratório. Não prepara o wrapper no GTA, não escreve memória, não instala CALL e não autoriza o primeiro callback físico. Trabalho realizado por um único agente, sem pesquisa externa.

## Diff e contratos

- `native_counter_admission.hpp`: planejador puro de CALL, compartilhado pelo dry-run e pelo writer transacional existente. Exige READY, PIN declarado, wrapper capaz, ausência de instalação/shutdown, versão e bytes originais coerentes; compara os cinco bytes observados, decodifica o destino original e calcula o CALL para o wrapper com o codec conservador existente. O resultado contém valores e bytes, sem handle ou capacidade de escrita.
- `native_counter_preflight.{hpp,cpp}`: `dryRunNativeCounterAdmission` só produz plano depois do preflight Hoodlum existente. Reutiliza reconhecimento por PE32/i386/tamanho/SHA-256, base, associação do arquivo carregado, exports/seções, snapshots estáveis, identidade do descriptor, endereços e CALL. Qualquer recusa retorna sem plano; destino do wrapper fora do alcance conservador também é recusado.
- `native_counter_transaction.cpp`: consome o mesmo planejador antes de qualquer alteração de proteção. Seus gates de evento proprietário, contextos, memória, readback e rollback continuam necessários. O writer permanece restrito ao host descartável; não foi vinculado ao diagnóstico nem ao ASI.
- `native_counter_diagnostic.cpp`: opção exclusiva `--dry-run` no diagnóstico existente. Conserva somente `PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION`, exige Hoodlum e associação do módulo à imagem esperada; lê CALL entre dois snapshots READY. Não chama exports, não cria evento de debugger e não executa escrita. `--lab-profile` não pode ser combinado com `--dry-run`.
- Testes/CMake: plano positivo com evidência sintética e entradas preservadas; recusas de identidade/base/módulo/snapshot/estado/PIN/capacidade/instalação/shutdown/endereço/versão/bytes/destino/truncamento/alcance. Teste adicional carrega o ASI experimental real em processo de teste, espera READY/PIN e comprova recusa sem modificar o descriptor. O teste de diagnóstico também recusa o host como Hoodlum em `--dry-run`.

O modo novo mantém a sintaxe do diagnóstico, acrescentando `--dry-run` a `--pid PID --expected-asi ARQUIVO`. Retornos: 0 para evidência admissível e plano calculado; 4 para estado/capacidade ainda não prontos; 3 para evidência incompatível/incompleta; 2 para argumentos inválidos. `writeEnabled=0` continua fixo. Não foi invocado contra GTA nesta tarefa.

As referências Hoodlum já documentadas permanecem: CALL `0x53E981`, bytes `E8 5A D5 FF FF`, destino `0x53BEE0`; ver [transação existente](external-patch-transaction.md) e [preparação anterior](native-game-thread-integration.md). Nenhum offset, API nativa ou fonte nova foi introduzido.

## Validação executada

Visual Studio 18 2026, configuração existente `build/alpha036-win32`, plataforma Win32, Debug. Build dos quatro targets abaixo terminou com código 0; dependências do bootstrap/wrapper/host também foram compiladas ou verificadas. O target `grandmp_windows` foi excluído para preservar `GrandMP.asi`.

```powershell
cmake --build build/alpha036-win32 --config Debug --target grandmp_native_counter_preflight_tests grandmp_native_counter_diagnostic grandmp_native_counter_transaction grandmp_native_counter_call_tests --parallel 4
ctest --test-dir build/alpha036-win32 -C Debug --output-on-failure -R 'native_counter_(preflight|call_encoding|async_|inert_asi)' -E '^native_counter_preflight_file$' --output-log build/alpha036-win32/admission-dry-run-ctest.txt
git diff --check
```

CTest: **20/20 aprovados**, 1,68 s, código 0. Inclui os novos `native_counter_preflight_admission`, `native_counter_preflight_admission_rejections` e `native_counter_preflight_actual_ready`, além do diagnóstico ampliado e regressão de bootstrap/codec. Log ignorado: `build/alpha036-win32/admission-dry-run-ctest.txt`.

A suíte transacional que instala patches no host não foi executada nesta slice, pelo limite explícito de não instalar hooks. O writer foi compilado e seu planejador compartilhado foi exercitado pelos testes puros. `native_counter_preflight_file` também foi excluído porque seu cleanup apaga arquivo temporário. Portanto estes resultados não constituem execução da suíte CTest completa nem validação física do writer. Revisão local do diff feita pelo próprio agente; nenhum subagente foi utilizado.

`git diff --check` passou. SHA-256 do `GrandMP.asi` em `build/alpha036-win32/client/windows/Debug/` antes e depois: `1421524621D938C2E06F3FF8E8D48298F8332C6B7982E2AEF6027C9B404EB45B`. Working tree inicial limpo; alterações limitadas aos arquivos descritos e este relatório. Nenhum GTA executado, arquivo instalado/removido ou commit realizado.

## Bloqueios para o primeiro callback físico

1. O bootstrap real continua publicando residência apenas: `wrapperCapable=0`, `callsite=0`, `originalTarget=0`, `installed=0`. O dry-run o recusa mesmo com READY/PIN. Preparação/binding do wrapper no Hoodlum ainda não foi implementada nem autorizada; `PrepareDeferred` permanece restrito ao host cooperativo. O positivo usa evidência sintética de preparação completa.
2. O diagnóstico associa snapshots e arquivo carregado; PIN é declarado pelo descriptor do binário admitido, não uma consulta independente do refcount Windows. Leituras estáveis não representam exclusão de execução nem reserva do estado para escrita futura.
3. Não há writer GTA nem posse de evento all-stop/contextos comprovados pelo dry-run. Um plano não pode substituir os gates transacionais antes de uma eventual escrita. Configuração do loader/plugins e demais condições do smoke físico continuam pendentes para essa finalidade.
4. ABI/fase do CALL no Hoodlum, forwarding exatamente uma vez e contador físico ainda exigem validação específica autorizada. O TID do worker READY não demonstra thread de motor. Os limites de SSE2/FXSAVE e os demais gates documentados continuam vigentes.

Implementação encerrada para revisão; nenhum passo físico iniciado.


## Fechamento técnico — compatibilidade com o writer

Revisão da preparação real confirmou compatibilidade: magic/version/size vêm da inicialização da exportação; `prepareTransaction` publica state=READY, residencyConfirmed=1, wrapperCapable=1 e os endereços do host/módulo. installed/shutdownLogical/reserved permanecem zero. originalBytesLow/High vêm dos mesmos cinco bytes E8 cujo destino foi validado antes da publicação. O writer valida identidade/exports e esses campos antes do planejador, conserva a posse do evento/contextos e relê os bytes antes de alterar proteção. Não foi necessária correção de lógica nem flexibilização de gates. Única correção de código neste fechamento: includes diretos `<cstdint>`, `<cstring>`, `<optional>` e `<span>` em `native_counter_admission.hpp`.

Antes de executar patches, foi conferido o isolamento: a CLI não aceita PID; exige `GrandMPNativeCounterLab.dll` no diretório do utilitário; cria exclusivamente o host vizinho `grandmp_native_counter_host.exe` com DEBUG_ONLY_THIS_PROCESS. Escrita/rollback/término usam somente o handle retornado dessa criação, com verificação da identidade do host e da DLL. O supervisor FATAL apenas observa o PID emitido pelo filho com SYNCHRONIZE/QUERY_LIMITED_INFORMATION. Arquivos do host/DLL são abertos GENERIC_READ; não há escrita em arquivos/backups nesses cenários. Esta execução usa a autorização posterior para patches apenas no laboratório, superando a omissão transacional registrada na validação inicial.

Build Win32 Debug dos mesmos quatro targets: código 0. CTest abaixo: **32/32 aprovados**, 6,69 s, código 0. Foram executados os nove preflights `identity`, `ready`, `module`, `call`, `policy`, `admission`, `admission_rejections`, `actual_ready`, `actual_asi`; o codec `native_counter_call_encoding`; e todos os 22 `native_transaction_*`: success, repeat, unexpected_bytes, third_party, offset_range, short_write, protect_failure, flush_failure, readback_failure, restore_failure, commit_failure, identity, version, invalid_state, no_residence, context_failure, context_overlap, inactive_event e fatal_write/fatal_flush/fatal_protection/fatal_commit. Log: `build/alpha036-win32/admission-closeout-ctest.txt` (ignorado).

```powershell
ctest --test-dir build/alpha036-win32 -C Debug --output-on-failure -R '^(native_transaction_|native_counter_preflight_|native_counter_call_encoding$)' -E '^native_counter_preflight_file$' --output-log build/alpha036-win32/admission-closeout-ctest.txt
```

Omitido `native_counter_preflight_file`: cria, escreve e exclui arquivo temporário, incompatível com a restrição deste fechamento. Demais suítes fora desse filtro não foram reexecutadas por não pertencerem à compatibilidade focada. Nenhum teste transacional foi omitido. O alcance rel32 do planejador também foi coberto pelo preflight negativo; o cenário transacional offset_range continua recusado antes de mutação (o wrapper injetado diverge do descriptor admitido).

`git diff --check` passou e o SHA-256 de `GrandMP.asi` foi reconfirmado idêntico ao registrado acima. Alterações anteriores preservadas; somente o header e este adendo foram modificados neste fechamento. Slice tecnicamente pronta para commit do dry-run; commit não executado. Os bloqueios físicos anteriores permanecem, sem homologação de callback no GTA.


## Preparação controlada do wrapper — base c25bf63

A restrição anterior era explícita: `PrepareDeferred` exige nome/exports do host cooperativo; `prepareTransaction` exige o gate ativado pela preparação cooperativa; o worker assíncrono desativava esse gate e não vinculava originalUpdate. A exportação cooperativa permanece com a mesma restrição. A preparação Hoodlum agora ocorre exclusivamente no worker existente, com opt-in adicional exato `GRANDMP_NATIVE_COUNTER_PREPARE_HOODLUM=1`, além do opt-in existente `GRANDMP_NATIVE_COUNTER_ASYNC_READY=1`. Não há nova exportação ou chamada síncrona de preparação em InitializeASI/DllMain.

O worker mantém a referência contada/PIN e constrói dados privados enquanto o descriptor permanece INITIALIZING. Confere FXSR/SSE/SSE2, fingerprint Hoodlum pelo leitor/reconhecedor existente, base/PE32 mapeados e páginas MEM_IMAGE/COMMIT/EXECUTE_READ do CALL e destino. Lê cinco bytes via ReadProcessMemory do próprio processo. `prepareNativeCounterHoodlumDescriptor`, no preflight existente, admite somente estado INITIALIZING/PIN sem binding/instalação/shutdown, constrói o candidato e o submete ao mesmo dry-run/planner. Os bytes e páginas são reconferidos antes de retornar. Arquivo, crypto e operações de loader ficam fora de preparationMutex.

Somente com candidato válido e sem cancelamento o worker vincula originalUpdate a `0x53BEE0`, publica `callsite=0x53E981`, bytes originais e `wrapperCapable=1`, com READY publicado por último via release. O campo state não recebe escrita não atômica. O descriptor continua v1/80 bytes; `installed=0` é preservado. Sem opt-in ou com erro, não há binding: mantém apenas READY/PIN com capacidade e campos do CALL zerados. Falha de residência preserva FAILED; shutdown concorrente impede republicação. Não há retry/upgrade de READY já publicado.

Preparar não invoca original, wrapper ou motor, nem altera código do jogo. Se futuramente despachado sob os gates independentes, o wrapper mantém o forwarding original uma vez e o gate/contador existentes; shutdown desativa contagem e preserva forwarding/residência. Nenhum CALL foi instalado nesta slice.

Validação: build Win32 Debug dos targets `grandmp_native_counter_preflight_tests`, `grandmp_native_counter_host`, `grandmp_native_counter_diagnostic`, `grandmp_native_counter_call_tests`, código 0. **39/39 CTests aprovados**, 1,45 s, código 0:

```powershell
ctest --test-dir build/alpha036-win32 -C Debug --output-on-failure -R '^native_counter_' -E '^native_counter_preflight_file$' --output-log build/alpha036-win32/hoodlum-preparation-ctest.txt
```

Novos testes: candidato positivo com evidência sintética Hoodlum (entradas preservadas, installed=0, dry-run admissível); recusas de identidade/arquitetura/base, estados inválidos, PIN, campos incompatíveis, shutdown/installed/reserved, módulo truncado, bytes/destino/truncamento/alcance; ASI real com opt-in ativo recusa o processo de teste não Hoodlum, sem capacidade/binding e sem alterar installed. Regressões existentes exercitaram forwarding/ABI do mesmo wrapper no host, concorrência, referência/PIN, cancelamento/shutdown e falhas do bootstrap. O positivo do coletor/binding Hoodlum no worker não foi executado: exigiria o GTA, proibido nesta slice. Falhas CPU/página/IO não foram injetadas individualmente no worker; esses gates foram inspecionados. O teste preflight_file foi excluído por escrever/excluir arquivo temporário; testes transacionais de patch ficaram fora desta rodada, que não altera o writer.

`git diff --check` passou. GrandMP.asi conserva SHA-256 `1421524621D938C2E06F3FF8E8D48298F8332C6B7982E2AEF6027C9B404EB45B`. Novo experimental: `A75A063983611916231FAC6CC0E82FA7A89EEF5BBAE871C79D85D40421ABC8C9`; fingerprints de smoke antigos não admitem automaticamente esta build. Arquivos alterados: CMake do cliente/testes, experimental, preflight hpp/cpp, testes de preflight e este adendo. Sem modificações do wrapper assembly, descriptor v1, writer ou produção; sem GTA/instalação/commit/push/merge. Build e testes passaram na primeira rodada, sem correção posterior de lógica.

Continuam bloqueados para o primeiro callback físico: validação autorizada do novo binding no Hoodlum, parada exclusiva e contextos, integração autorizada do writer GTA, ABI/fase/forwarding físicos e configuração/binários admitidos. As leituras do worker não oferecem exclusão de threads nem garantia futura contra outro patch; o writer deverá revalidar sob sua parada proprietária. READY/wrapperCapable são dados de preparação, nunca autorização de instalação. O diagnóstico padrão permanece exclusivo de residência; a evidência preparada pertence ao modo `--dry-run`. Revisão local realizada pelo único agente, sem pesquisa externa.
