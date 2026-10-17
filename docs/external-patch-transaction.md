# Alpha 0.3.6 / Slice 4 — External Patch Transaction

## Decisão e alcance

**GO para a transação no host contador descartável. NO-GO para smoke de callback no GTA com os artefatos atuais.** O problema da escrita de cinco bytes com executores concorrentes recebeu uma implementação e teste real sob evento Windows pendente. O writer aceita exclusivamente o host e a DLL de laboratório desta build; não aceita PID, executável ou endereço GTA por argumento.

Esta entrega concretiza o writer/READY/wrapper propostos na [Slice 3](native-game-thread-integration.md). As descrições históricas das Slices 1 e 3 de que esses componentes ainda não existiam continuam válidas para suas entregas, mas foram superadas **no laboratório** por esta slice. O `GrandMPNativeCounterExperimental.asi` continua inerte: não exporta READY, preparação transacional ou wrapper. `GrandMP.asi` homologado e `asi_entry.cpp` não foram alterados. Nenhum jogo foi executado, arquivo copiado para GTA ou staging/commit realizado.

Relação com [ROADMAP.md](ROADMAP.md): progresso concreto em N1 (instalação exclusiva, residência, forwarding e contador); N1 físico permanece pendente, assim como N2/IPC, budget p99, soak e dez ciclos de partida. Não há ligação ao adapter, entidades ou rede no wrapper.

## Arquivos desta slice

| Caminho | Responsabilidade |
|---|---|
| `client/windows/tools/native_counter_ready.hpp` | Descriptor READY v1, layout explícito x86, sem chamadas remotas |
| `client/windows/tools/native_counter_call.hpp` | Encode/decode de CALL rel32 com limites verificados |
| `client/windows/tools/native_counter_transaction.cpp` | Debugger externo específico, validações, instalação, rollback e política FATAL |
| `client/windows/tools/native_counter_host.def` | Exportações conhecidas do código do host para validação do debugger |
| `client/windows/tools/native_counter_experimental.cpp` | Preparação/publicação e wrapper somente no ramo da DLL de laboratório |
| `client/windows/tools/native_counter_lab.def` | Exportações adicionais somente do laboratório |
| `client/windows/tools/native_counter_host.cpp` | Cenário `debug_target`, concorrência, ABI, residência e shutdown real |
| `tests/native_counter_call_tests.cpp` | Codec portátil e referência dos bytes Hoodlum, sem acessar GTA |
| `tests/native_transaction_runner.py` | Supervisor independente dos filhos encerrados em FATAL |
| `client/windows/CMakeLists.txt`, `tests/CMakeLists.txt` | Targets, flags de auditoria e testes |
| `docs/external-patch-transaction.md` | Contratos, evidência, separação Git e decisão |

Os demais diffs acumulados pertencem às Slices 1/2/3 e foram preservados.

## READY e residência

O mesmo host contador existente carrega `GrandMPNativeCounterLab.dll`. `InitializeASI` apenas solicita a inicialização; a preparação explícita é feita pelo host cooperativo, fora de DllMain e de LoadLibrary. A DLL valida sua própria identidade por FROM_ADDRESS e adquire PIN uma única vez pelo bootstrap existente. PIN é irreversível até exit. FreeLibrary da referência externa não permite descarregar o código ainda referenciado pelo CALL. Não há hot-unload nem restauração executável por destrutor/DllMain.

`GrandMPNativeCounterReady` é uma exportação PE DATA de armazenamento estático residente: 80 bytes, vinte campos `uint32_t`, alinhamento natural. `magic=0x47524D50`, `interfaceVersion=1`, `size=80`. A interface é local Win32, little endian; não constitui um protocolo de rede.

| Campos | Uso |
|---|---|
| `processId`, `moduleBase`, `moduleSize`, `descriptorAddress` | Identidade e localização confrontadas com os eventos e exports PE |
| `state` | 0 dormant → 1 requested → 2 ready; 3 disabled ou 4 failed; InitializeASI repetido não reativa estado desligado |
| `residencyConfirmed`, `wrapperCapable`, `shutdownLogical` | READY exige 1, 1 e 0 respectivamente |
| `wrapper`, `originalTarget`, `callsite`, `counterAddress` | Endereços x86; endereços de código precisam corresponder aos exports da build admitida |
| `threadId` | Thread que publicou READY, confrontada com o evento |
| `originalBytesLow`, `originalBytesHigh` | Cinco bytes do CALL original, confrontados com encode/decode e memória |
| `installed` | Publicação exclusiva do writer: 0 antes/rollback; 1 apenas após confirmação completa |
| `reserved` | Deve ser zero |

O host publica READY depois de preparar o original imutável e validar suporte FXSR/SSE/SSE2. Só então emite `OutputDebugStringA("GRANDMP_NATIVE_COUNTER_READY_V1")`. O debugger conserva **esse OUTPUT_DEBUG_STRING_EVENT real pendente**; lê o descriptor via ReadProcessMemory. Não chama exports, `.call` ou qualquer código do alvo parado. O marcador isolado não basta: todos os campos e imagens são conferidos. Esta escolha substitui, no laboratório, o breakpoint READY sugerido pela Slice 3; não introduz STATUS_SINGLE_STEP.

## Exclusão e admissão do writer

A [documentação Microsoft de eventos de depuração](https://learn.microsoft.com/en-us/windows/win32/debug/debugging-events) estabelece suspensão das threads do processo afetado enquanto o evento está pendente. O debugger inicia somente seu próprio filho com DEBUG_ONLY_THIS_PROCESS. Um token privado é emitido depois de WaitForDebugEvent e invalidado pela continuação bem-sucedida; não existe parâmetro público `allStopped=true`.

Antes de qualquer mutação:

1. Exigir DLL `GrandMPNativeCounterLab.dll` e host `grandmp_native_counter_host.exe` na pasta do próprio writer. Ambos são arquivos locais PE32/i386 desta execução; o arquivo esperado fica aberto sem compartilhamento de escrita. Comparar identidade de arquivo e conteúdo completo com os handles fornecidos pelos eventos de carga; conferir headers remotos, base, tamanho e timestamp.
2. Resolver exports das imagens conhecidas, recusando forwarders e endereços fora das seções apropriadas. O descriptor não pode escolher outro alvo por conta própria.
3. Validar READY, PIN, versão, estado e endereço do contador. Recusar estado desligado, instalado, desconhecido ou preparação incompleta.
4. Confrontar o conjunto de threads obtido pelos eventos de debug com Toolhelp32Snapshot. Obter contextos de **todas** essas threads e recusar falha, conjunto divergente ou EIP no intervalo dos cinco bytes que serão modificados.
5. Exigir página MEM_IMAGE, MEM_COMMIT, PAGE_EXECUTE_READ, pertencente ao host; os cinco bytes devem caber na mesma página/região.
6. Validar CALL E8 completo, original e destino. O codec rejeita endereços zero/fora de 32 bits, overflow de source+5 e deslocamento fora de signed rel32. Este perfil conservador não aceita wrap modular de endereços, mesmo quando arquiteturalmente possível em x86.

A parada exclui os executores **do alvo**; não impede um terceiro processo de escrever nele, adulterar o debugger ou modificar o patch posteriormente. Esse não é um mecanismo de proteção contra adversários. A tentativa repetida recusa bytes já instalados; não encadeia nem sobrescreve hooks de terceiros.

## Transação e falhas

Dentro do mesmo evento pendente, o writer reconfirma os bytes originais imediatamente antes de VirtualProtectEx. Guarda proteção original e bytes; altera temporariamente a proteção, escreve exatamente cinco bytes, lê novamente e decodifica o destino, chama FlushInstructionCache, restaura proteção e confirma-a com VirtualQueryEx. Somente então escreve/readback `installed=1`. **WriteProcessMemory não é presumido atômico ou transacional.**

| Resultado | Condição / continuação |
|---|---|
| REJECTED | Pré-condição rejeitada antes de escrever; proteção original confirmada quando a chamada de proteção falha. Host segue sem contador instalado. |
| INSTALLED | Bytes, destino, flush, proteção e publicação integralmente confirmados. Somente agora o evento pode ser continuado. |
| ROLLED_BACK | Após falha parcial: repor flag 0, bytes originais, readback, flush e proteção sob o mesmo stop; todos confirmados. Host segue pelo original. |
| FATAL | Estado consistente não demonstrado. Não continuar evento nem fazer detach. Solicitar TerminateProcess **apenas do filho descartável**, sair com kill-on-exit habilitado; supervisor externo verifica o término real. |

Windows pode postergar teardown enquanto o evento permanece retido. Por isso, aguardar a saída no próprio debugger ainda parado não demonstrava término. O [kill-on-exit documentado](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-debugsetprocesskillonexit) e o supervisor separado permitem confirmar o filho encerrado sem retomar instrução indeterminada. Os testes FATAL exigem rollback iniciado, tentativa de escrita e duas falhas injetadas consumidas; não passam por um FATAL anterior à mutação.

O supervisor tem prazo interno de 15 s para saída; debugger 12 s para eventos e CTest 20 s por caso. Em falha/timeout, a política do laboratório é encerrar seu filho, sem retomar patch indeterminado. Exceções desconhecidas recebem DBG_EXCEPTION_NOT_HANDLED; somente o primeiro breakpoint de sistema, na thread inicial e na imagem ntdll, é reconhecido como evento de inicialização. Nenhuma exceção de aplicação é convertida genericamente em sucesso.

## Wrapper x86 e evidência de ABI

O CALL do host é uma função naked `void __cdecl()` sem argumentos. Seu original tem retorno conhecido e incrementa seu próprio contador. O wrapper chama o original **uma vez**, salva o estado produzido por ele, aplica apenas o gate residente e incremento atômico, restaura estado e retorna ao chamador.

Auditoria de dumpbin MSVC 14.51.36260 na build final:

- CALL indireto para original; PUSHFD/PUSHAD; área de 528 bytes alinhada a 16; ESP salvo fora dos 512 bytes de FXSAVE; FXSAVE → helper → FXRSTOR; POPAD/POPFD/RET.
- O helper não usa engine, rede, alocação, I/O ou mutex; o incremento é LOCK CMPXCHG8B. A função de contador executa somente `fetch_add`.
- Um teste real inicial detectou XMM0 alterado: a implementação Debug de leitura atômica gerava MOVQ em XMM0. A versão final preserva x87/MMX/MXCSR/XMM com FXSAVE/FXRSTOR. O host verifica GPRs, flags aritméticas, ESP, sentinela XMM0 e x87.
- CALL do host auditado: `E8 DB FF FF FF` em `0x00401020` → `0x00401000`; endereços são resolvidos pela imagem carregada, não hardcoded no writer. Esses bytes **não são** os bytes GTA.
- Sem instruções AVX/YMM na build auditada. O contrato atual cobre x86/SSE2; FXSAVE não prova preservação de estado AVX superior, ABI arbitrária, unwind ou exceções de outra função. A ABI completa do motor continua a exigir auditoria física/específica antes de reaproveitar o wrapper nele.

Saída final real: `installed=1 original=8111 count=8001 ABI=ok resident=1 shutdown=1`. São 1 chamada de ABI + 8.000 chamadas em quatro threads; após desativação, 100 chamadas continuam pelo original sem incrementar; após FreeLibrary, mais 10 também continuam pelo código residente. Desativação lógica não remove o CALL ou interrompe uma operação já admitida em outra thread. Após quiescência, não há novos incrementos; o código permanece válido até exit.

## Cobertura e resultados executados

| Casos | Evidência |
|---|---|
| success / repeat | Instalação real externa, duplicação rejeitada, quatro executores concorrentes, original exatamente uma vez por chamada, ABI, shutdown e PIN |
| unexpected_bytes / third_party | Mismatch injetado no guard; e CALL estrangeiro **real** no alvo, preservado sem mutação pelo writer |
| offset_range | Recusa de rel32 fora do perfil aceito |
| identity / version / invalid_state / no_residence | Descriptor negativo sintético sobre snapshot real; nenhuma instalação |
| context_failure / context_overlap / inactive_event | Contextos reais com falha/overlap injetados; token realmente invalidado por continuação, sem escrever com token expirado |
| short_write / readback_failure | Escrita real de dois bytes e leitura parcial injetada; rollback/readback completos |
| protect_failure / flush_failure / restore_failure | Falhas injetadas na camada das APIs reais; recusa ou recuperação confirmada |
| commit_failure | Publicação de dois bytes realmente executada, recusada e revertida |
| fatal_write / fatal_flush / fatal_protection / fatal_commit | Falha da instalação e da recuperação; evento não continuado, filho realmente encerrado/ausente observado por supervisor independente |
| codec portátil | Round-trip, referência Hoodlum `E8 5A D5 FF FF` → `0x53BEE0`, limites e instruções truncadas/incompatíveis; sem processo GTA |

As falhas de API/contexto são injeções explícitas, não alegações de que o sistema operacional produziu espontaneamente esses erros. Não substituem o teste real de debug/evento/escrita/readback/saída.

Validação final Debug, Visual Studio 18 2026 / MSVC 19.51.36260 / SDK 10.0.26100.0:

- **Win32: 145/145 CTest, 12,94 s**, build exit 0. Inclui os 122 testes anteriores + codec + 22 cenários de transação externa.
- **x64: 111/111 CTest, 2,66 s**, build exit 0. Inclui os 110 anteriores + codec; writer e wrapper funcional são exclusivos Win32.
- Foco anterior na nova cobertura: **23/23**. Regressões de observer/protocolo/servidor/adapter e loopback existente passaram na suíte completa.

Comandos executados:

```powershell
cmake -S . -B build/alpha036-win32 -G "Visual Studio 18 2026" -A Win32
cmake --build build/alpha036-win32 --config Debug --parallel 4
ctest --test-dir build/alpha036-win32 -C Debug --output-on-failure
cmake -S . -B build/alpha036-x64 -G "Visual Studio 18 2026" -A x64
cmake --build build/alpha036-x64 --config Debug --parallel 4
ctest --test-dir build/alpha036-x64 -C Debug --output-on-failure
```

Logs: `build/alpha036-{win32,x64}/slice4-build.txt`, `slice4-ctest.txt`, `slice4-ctest-detail.log`; disassembly `build/alpha036-win32/slice4-wrapper-disassembly.txt`. Todos são artefatos ignorados.

## Execução manual independente

O comando abaixo inicia **somente o host descartável** e termina após as verificações. Não recebe PID GTA ou copia arquivos:

```powershell
Set-Location -LiteralPath 'D:\Programacao\Workspace\Applications\grandmp'
& '.\build\alpha036-win32\client\windows\Debug\grandmp_native_counter_transaction.exe' '.\build\alpha036-win32\client\windows\Debug\GrandMPNativeCounterLab.dll' success
```

Esperado: INSTALLED, host PASS com contagens acima e `debug_transaction_host_exit=0`. FATAL é testado por CTest/supervisor; não deve ser interpretado como permissão de continuar um processo arbitrário.

## Separação Git preservada

Baseline `af3af1c90c61216dfc153a2f51b14f2368e2c8bb`; índice permanece vazio. A proposta de commits das Slices 1/2 está na Slice 3. **Agora não basta adicionar os arquivos counter inteiros ao commit da Slice 1**, pois `native_counter_experimental.cpp`, `native_counter_host.cpp` e `native_counter_lab.def` também contêm hunks novos da Slice 4.

- Slice 1: targets counter base, testes counter base, gate/ASI inerte/host inicial e seu relatório, antes das exportações READY/wrapper/debug_target.
- Slice 2: targets scene host/pipeline/loopback, hunks observer/adapter e seu relatório/ROADMAP, incluindo correções de fechamento da Slice 3 já atribuídas a ela.
- Slice 3: `docs/native-game-thread-integration.md` e evidência/decisão histórica.
- Slice 4: doze paths acima, selecionando somente seus hunks quando compartilhados.

Hunks compartilhados de `client/windows/CMakeLists.txt`: scene host pertence à Slice 2; bloco counter base à Slice 1; bloco final `/JMC-`, `/INCREMENTAL:NO`, `/DEF` e target transaction à Slice 4. Em `tests/CMakeLists.txt`: pipeline/loopback à Slice 2; counter base à Slice 1; codec e transaction/supervisor à Slice 4. Estados intermediários devem ser compilados/testados quando commits forem autorizados; nenhuma separação ou staging foi aplicada aqui.

## Revisões e gates ainda físicos

- **Architect:** sem bloqueador adicional na transação de laboratório; recomendação de prazo interno no supervisor implementada. Propriedade do evento é concreta, callback/estado residentes e FATAL não retoma execução. GO laboratório, NO-GO GTA.
- **Windows native:** implementou READY/wrapper no host existente, identificou e corrigiu clobber SIMD e auditou o binário final. O perfil de preservação acima é demonstrado no host; não homologado no motor.
- **Reviewer read-only:** confirmou correções FP/publicação parcial, assertions de recuperação e supervisor; sem bloqueador adicional no laboratório. GO laboratório, NO-GO GTA. Os testes foram executados pelo agente principal.

O passo concreto mínimo para permitir um **smoke posterior autorizado** é conectar a preparação leve do ASI experimental a uma entrada admitida no GTA e publicar o mesmo READY residente **antes** da parada externa. Isso ainda não está implementado: não executar export arbitrária via debugger, não assumir InitializeASI sempre livre, não criar thread de engine. O writer futuro precisa acrescentar fingerprint Hoodlum/loader/configuração e inventário admitido de plugins em vez da identidade estrita do host, auditar o wrapper para o call-site real e manter os mesmos gates all-stop/contextos/rollback. Essa adaptação não está incluída automaticamente nesta slice.

| Gate para GTA | Situação |
|---|---|
| CALL/destino/identidade Hoodlum | Evidência física anterior preservada: `0x53E981`, `E8 5A D5 FF FF`, `0x53BEE0`, SHA-256 homologado; não reexecutada |
| Exclusão externa durante escrita | Demonstrada em debuggee real descartável, fundamentada pelo contrato Windows |
| READY e PIN no ASI físico | **Ausentes**; ASI atual inerte |
| ABI completa / original no motor | Evidência anterior mais wrapper testado; **não homologado em GTA** |
| Thread-affinity e comportamento em menu/loading/partida | **Não demonstrados para dispatch do novo wrapper** |
| Terceiros | Divergência atual recusa; não garante proteção contra writers externos/patch posterior |
| Budget/soak/10 ciclos/N2 | Pendentes; Alpha 0.3.6 não concluída |

Um futuro smoke deverá obter contador/thread após o original em menu confirmado, loading e partida, sem engine/entidades, e encerrar normalmente. Antes da escrita: abortar por identidade/bytes/contexto/READY incompatíveis. Após falha parcial: rollback confirmado na mesma parada ou término controlado, nunca resume indeterminado. Depois de retomar: nenhum hot-unload/restore; rollback de arquivos só com processo encerrado, retirando experimental opt-in e preservando o ASI homologado. **Não há autorização de smoke GTA nesta entrega.**

### Alternativa futura: vtable do CubicMP

A alternativa indicada de substituição de entrada de vtable permanece separada. Em princípio, escrever um ponteiro alinhado pode evitar a escrita rel32 de cinco bytes, mas ainda exige identidade/ABI da vtable e objeto, publicação, propriedade, exclusão/lifetime e residência; não resolve esses contratos por si só. Não foi fornecida uma revisão verificável do CubicMP e a busca pública não identificou uma fonte confiável correspondente. Portanto não atribuímos endereços, layouts, licença ou garantias a ele. Nenhum código vtable ou mecanismo concorrente foi incorporado nesta slice. A opção exige fonte/revisão verificável antes de avaliação específica.
