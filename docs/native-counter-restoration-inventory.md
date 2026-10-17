# Alpha 0.3.6 — inventário da parada de restauração

Base: `30afb1f9cf897c87c3d530bf496ac3f388f35e7e`. Um agente, sem subagentes. Escopo: validador de inventário/contextos, fixture do host, runner supervisionado, DLL descartável, registro CTest e relatório. Sem GTA, attach ao jogo, patch físico, cópia para a instalação, commit ou push.

## Causa demonstrada

A implementação original usa `externalInventory(true)` nas duas paradas. `modules != beforeModules_` recusa cargas/descargas; a obrigação de sobrevivência dos handles pré-attach recusa saída legítima mesmo após EXIT_THREAD retirar a thread do mapa. Já `ids.size() != threads_.size()` compara snapshot com eventos: essa igualdade continua necessária, não deve ser retirada nem substituída pela quantidade inicial.

Foi compilada em Debug|Win32 uma cópia **sem alterações** do `.cpp` de `30afb1f`, extraída com `git show` em `build/alpha036-win32/client/windows/inventory_baseline.cpp`. Usou cópia isolada do projeto MSBuild do supervisor, includes/bibliotecas existentes, `BuildProjectReferences=false`, nome de saída `grandmp_native_counter_transaction_baseline.exe` e IntDir próprio. Os fixtures atuais provocaram separadamente, após installed=1:

- `baseline_threads_exit`: encerra uma thread pré-attach, mantendo o coordenador e os dispatchers vivos;
- `baseline_modules_add`: carrega `GrandMPNativeInventoryProbe.dll`, mantendo as threads do fixture vivas.

Nos dois casos: INSTALLED → pedido da segunda parada → `external inventory changed` → HELD, receipt=1, installed=1, CALL para o wrapper e proteção RX. O supervisor independente leu CALL/installed e confirmou alvo/debugger vivos. Logs: `build/alpha036-win32/inventory-baseline-threads-exit.txt` e `inventory-baseline-modules-add.txt`. O antigo error=18 é ERROR_NO_MORE_FILES residual da enumeração, não a causa Win32 da recusa.

Isso demonstra a causa lógica no laboratório. O log antigo do gameplay não distingue qual condição concreta falhou; o alvo real não foi inspecionado nesta execução.

## Contrato corrigido

`prepareExternalInventory`, `admissionInventory` e `restorationInventory` identificam as fases. Restauração exige receipt, autorização e pedido de break pendentes. As duas paradas exigem token ativo da sessão/thread proprietária, PID correto e thread do evento contabilizada.

Na **admissão**, os módulos precisam permanecer iguais ao pré-attach; as threads pré-attach precisam estar vivas e corresponder por tempo de criação aos handles dos eventos. Mudança antes da instalação continua recusada. Threads adicionais só passam se snapshot/eventos coincidirem e seus contextos forem válidos.

Na **restauração**, o inventário inicial serve para diagnosticar deltas. O estado atual precisa coincidir exatamente com os mapas de eventos:

- Cada módulo vivo tem base, caminho canônico, SHA-256 integral, volume/file ID, SizeOfImage e timestamp PE iguais aos registrados pelo hFile do evento. O supervisor abre seu próprio handle com GENERIC_READ e FILE_SHARE_READ no Image, compara identidade/bytes com o hFile original e transfere esse handle ao ModuleEvent até o unload. Não herda o compartilhamento do hFile. Não se retêm cópias integrais de todos os módulos em memória. O snapshot reabre e valida imagens, cabeçalhos remotos e mapeamento MEM_IMAGE. Comparação de caminhos do inventário usa CompareStringOrdinal sem distinguir maiúsculas, sempre acompanhada de file ID/hash; gates próprios de caminho do Hoodlum/ASI permanecem.
- Carga/descarga não protegida atualiza os mapas. Recarga na mesma base exige unload da instância anterior e load com a nova identidade. Descarga desconhecida, carga duplicada, snapshot sem evento, identidade diferente do evento ou backing não verificável recusam. Executável, ASI e ntdll não podem ser descarregados/recarregados. Bases admitidas do executável/ASI continuam fixas.
- Saída pré-attach exige EXIT correspondente à mesma instância, comparada pelos tempos de criação dos handles. Na segunda parada o handle original também precisa estar sinalizado. Saída sem essa evidência recusa mesmo quando os mapas de threads vivas coincidem. Uma nova instância exige novo CREATE e seu handle; TID sozinho não prova identidade.
- Snapshot e mapa das threads vivas devem ter exatamente os mesmos IDs. Todos os handles vivos precisam pertencer ao PID, estar não sinalizados e fornecer GetThreadContext sob a parada autenticada. Nenhum EIP pode estar em `[callsite, callsite + 5)`.

Identidade do processo inclui handle/PID, criação, caminho e backing do executável. A troca pelo handle com direitos de escrita mantém a comparação de criação já existente. Hash aprovado, scope Hoodlum, bases, READY/PIN, descriptor completo, receipt, CALL instalado, bytes/destino, páginas e proteções continuam exigidos na transação inversa. **Memory e transact não foram alterados**; binding/contextos recebem diagnóstico adicional.

`inventory_delta`, `inventory_module_delta`, `inventory_thread_event` e `inventory_pre_thread_exit` mostram alterações/identificadores. `inventory_refusal` registra fase, motivo e sujeito. APIs do inventário/contextos e dos leitores capturam GetLastError imediatamente quando falham; recusas lógicas usam win32=0. Leitura remota registra endereço/erro antes de outro trabalho. Não há conversão de erro indeterminado em evidência positiva, DLL ignorada ou drenagem de eventos por continue após recusa.

## Testes e reprodução

Seis positivos novos: `native_supervised_threads_create`, `threads_exit`, `threads_churn`, `modules_add`, `modules_remove` e `modules_reload`. O alvo aguarda a instalação real antes de modificar o inventário. O runner confirma mudança concluída com installed=1, reconciliação na segunda parada, RESTORED/detach, CALL/installed por leitor independente e self-check final de descriptor, forwarding e residência.

Oito negativos novos: registro omitido de CREATE real, confirmação omitida de EXIT real, LOAD_DLL real omitido, digest adulterado no snapshot em memória do **supervisor de laboratório**, perda do token, falha de contexto, EIP sobreposto e erro Win32 real de GetThreadContext(nullptr), exigindo ERROR_INVALID_HANDLE=6. Conservam installed=1/CALL para wrapper, alvo/debugger vivos e nenhuma restauração/continue/detach após a recusa. Limpeza pertence só ao runner criador daquele par. Injeções permanecem bloqueadas para Hoodlum.

Continuam os negativos existentes de identidade/descriptor, bytes/CALL de terceiro, contextos, perda de parada, autorização, proteção, escrita parcial, readback/flush/publicação, rollback e FATAL, incluindo restauração.

Build: Visual Studio 18 2026, SDK 10.0.26100.0, Debug|Win32. Somente transaction/host/inventory_probe/inventory_file_tests, com `/p:BuildProjectReferences=false`. ASIs não recompilados nem relinkados.

```powershell
cmake --build build/alpha036-win32 --config Debug --target grandmp_native_counter_transaction grandmp_native_counter_host grandmp_native_inventory_probe grandmp_native_counter_inventory_file_tests -- /p:BuildProjectReferences=false
ctest --test-dir build/alpha036-win32 -C Debug -R '^(native_transaction_|native_attach_|native_supervised_|native_counter_)' -E '^native_counter_preflight_file$' --output-on-failure --output-log build/alpha036-win32/inventory-restore-final-ctest.txt -j 1
```

Para reproduzir a base, após compilar a cópia original isolada junto dos fixtures atuais:

```powershell
python tests/native_counter_supervised_runner.py build/alpha036-win32/client/windows/Debug/grandmp_native_counter_transaction_baseline.exe build/alpha036-win32/client/windows/Debug/GrandMPNativeCounterLab.dll baseline_threads_exit
python tests/native_counter_supervised_runner.py build/alpha036-win32/client/windows/Debug/grandmp_native_counter_transaction_baseline.exe build/alpha036-win32/client/windows/Debug/GrandMPNativeCounterLab.dll baseline_modules_add
```

**Validação final: 136/136 aprovados em 62,85 s** (46 contador/preflight/codec/arquivo, 42 transacionais, 20 attach e 28 supervisionados). Log: `build/alpha036-win32/inventory-restore-final-ctest.txt`; saídas detalhadas em `Testing/Temporary/LastTest.log`. Build Debug|Win32 aprovado. `native_counter_preflight_file` fica fora por não exercitar o contrato alterado; `inventory_file_limits` está incluído porque os leitores privados receberam diagnóstico. A intermitência histórica de snapshot/eventos não é declarada resolvida.

SHA-256 e timestamp UTC dos ASIs locais preservados: experimental aprovado `AC0B839D5E524DDAD659588EC9B9894C2B0EDC1A07FEA8EB57DB23E2FDCC846C` (2026-10-16 00:34:07); produção `1421524621D938C2E06F3FF8E8D48298F8332C6B7982E2AEF6027C9B404EB45B` (2026-10-13 23:45:51). Arquivos instalados no jogo não consultados. Nenhuma alteração em CALL/ABI/wrapper/writer/ASIs/READY/PIN ou referência de aprovação. `git diff --check` aprovado.

## Análise separada: contenção pós-instalação

**HELD não foi eliminado.** Na segunda parada com evento proprietário, o alvo fica congelado e o CALL pode continuar instalado. Falha no pedido de break pode gerar HELD sem alvo parado. Não há prazo máximo garantido para restauração, que depende de gates/confirmação humana. PIN não drena callbacks em voo; ABI/fase do motor não estão homologadas fisicamente.

Encerrar só o debugger não é recuperação segura: antes de DebugSetProcessKillOnExit(FALSE), o padrão pode encerrar o alvo; depois de FALSE, a saída do proprietário provoca detach e pode permitir execução. Os testes existentes de disconnect/perda de token exercitam novamente esse risco. A documentação de [DebugSetProcessKillOnExit](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-debugsetprocesskillonexit), atualizada em 2024-02-22, descreve a política por thread proprietária.

Evitar depender de desligar o computador exige **mecanismo separado de descarte explícito do experimento**. Proposta para outro slice, não implementada: controlador independente, console acessível fora do alvo, identidade por handle/PID/criação, decisão humana específica de descarte, pedido de término apenas desse processo e confirmação por handle sinalizado. É necessário testar a conclusão do término com eventos pendentes e morte do proprietário antes de definir uma sequência física. [TerminateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess), atualizado em 2025-12-30, é assíncrono quando chamado de outro processo; retorno positivo não prova término completo.

O runner atual pede término ao alvo próprio, encerra seu debugger e aguarda ambos. Isso demonstra limpeza dos pares do laboratório; não autoriza transplantar esses kills para um PID do jogo nem prova sequência universal sem janela de execução. Uma VM descartável com controle no host daria uma fronteira adicional de descarte se o desktop convidado ficar inacessível; compatibilidade gráfica/GTA exigiria avaliação própria. Não há garantia contra bloqueio de driver/GPU ou do sistema nem diagnóstico do desktop que exigiu desligamento.

Continue forçado, detach, shutdown/unload do ASI, morte do proprietário primeiro ou restauração sem evidência completa continuam sem autorização automática. Nenhuma dessas ações foi acrescentada; nenhuma recuperação automática nova foi implementada.

## Limites e decisão

Módulo sem hFile utilizável, PE/fingerprint fora dos limites, arquivo inacessível, snapshot parcial/discordante, thread sem contexto ou erro de consulta continuam fail-closed. Podem ocorrer novas recusas específicas inclusive em eventos iniciais; não existe fallback por nome/base. DLL própria cobriu carga/descarga/recarga; substituição por outra DLL diferente na mesma base e reúso efetivo de TID não foram demonstrados. A lógica exige identidade/eventos correspondentes nesses casos. Nenhuma conclusão sobre os módulos/threads concretos do segundo gameplay.

**GO para revisão do diff de laboratório. NO-GO para nova execução física neste handoff**: revisão pendente, contenção de HELD e ABI/callbacks em voo não resolvidos. Nenhum próximo slice iniciado automaticamente. Revisão final read-only feita pelo próprio agente; sem revisão independente por subagente, conforme a restrição expressa do usuário.

Fontes Win32 consultadas em 2026-10-16, sem novos offsets do GTA: [DebugActiveProcess](https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-debugactiveprocess), [WaitForDebugEvent](https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-waitfordebugevent), [ContinueDebugEvent](https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-continuedebugevent), [LOAD_DLL_DEBUG_INFO](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-load_dll_debug_info). Fundamentam inventário inicial por eventos, propriedade/fechamento dos handles e possibilidade de hFile inválido, cuja ausência de evidência é recusada.
