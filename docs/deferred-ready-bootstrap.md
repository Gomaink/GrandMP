# Alpha 0.3.6 / Slice 6 — Deferred READY Bootstrap

**GO técnico para um futuro smoke autorizado somente de carregamento/READY. Callback e patch GTA continuam NO-GO.** Nenhum GTA foi executado ou modificado nesta slice.

## Implementação e lifetime

Opt-in por ambiente: `GRANDMP_NATIVE_COUNTER_ASYNC_READY=1`, definido antes de carregar o experimental. Sem esse valor exato, o comportamento cooperativo da Slice 5 permanece: InitializeASI somente REQUESTED; seus testes continuaram aprovados.

1. InitializeASI solicita o bootstrap e um CAS permite somente um agendamento. Adquire referência **contada** FROM_ADDRESS para o próprio módulo e confirma HMODULE antes de CreateThread. Não usa UNCHANGED_REFCOUNT, não espera o worker nem adquire o mutex de preparação.
2. A referência passa a pertencer ao worker. Cobre agendamento, início da thread e aquisição de PIN, mesmo se o loader liberar sua própria referência logo depois que InitializeASI retornar.
3. O worker usa somente Windows e dados do próprio módulo: valida PE32, adquire PIN e publica identidade/endereços/READY v1 por último. Aquisição/liberação de referências e PIN acontecem fora do mutex de preparação, evitando inversão com loader lock.
4. Contador permanece desativado durante todo o caminho. `callsite=0`, `originalTarget=0`, `wrapperCapable=0`, `installed=0`: **READY significa residência e diagnóstico, não capacidade de hook ou contexto de motor**. O preflight da Slice 5 continua recusando instalação com esses campos.
5. Worker termina com FreeLibraryAndExitThread fora de qualquer guard ativo. Falha/cancelamento nunca retorna para código possivelmente descarregado; sucesso conserva PIN até exit. Falha de referência/agendamento/PIN é fechada; SHUTDOWN não é reativado.

Contrato necessário: o loader mantém sua referência LoadLibrary até a chamada InitializeASI retornar. Unload concorrente enquanto outra thread ainda executa a exportação viola esse contrato e não é suportado. PIN é irreversível durante a sessão; shutdown é lógico, sem hot-unload.

Fontes oficiais: [GetModuleHandleExW](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw) documenta referência contada e PIN; [FreeLibraryAndExitThread](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-freelibraryandexitthread) evita a corrida entre liberar DLL e retornar/encerrar thread; [CreateThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread) documenta serialização com inicialização/notificações DLL. Não aguardar a thread permite ao chamador liberar seu contexto de carga; isso não identifica thread do motor nem mede LoaderLock no GTA.

## Código e validação

Alterados `native_counter_experimental.cpp`, `native_counter_host.cpp`, `native_counter_lab.def`, preflight `.hpp/.cpp` e os dois CMakeLists. Criados `native_counter_diagnostic.cpp`, `tests/native_counter_diagnostic_test.py` e este relatório. O único export de fault/gates novo fica na DLL de laboratório; o ASI real não oferece essas injeções. Não há alteração do ASI homologado, rede, servidor, Android ou adapter.

Testes novos no host existente:

- `async_ready`, `async_concurrent`: ASI real, apenas InitializeASI, sem PrepareDeferred; 32 chamadas concorrentes.
- `async_release`: ASI real, FreeLibrary imediatamente após InitializeASI; observa DATA via ReadProcessMemory até READY/PIN, sem referência de observador ou espera pelo início do worker.
- `async_unload`: gate exclusivo do laboratório confirma módulo vivo **antes do PIN**, após liberação da referência original, pela referência do scheduler; depois utiliza referência de observação para polling seguro.
- `async_shutdown`, `async_reference_failure`, `async_thread_failure`, `async_pin_failure`: não publicam READY, liberam referências e permitem unload normal. Falhas são injetadas explicitamente na DLL de testes, não alegações de erros espontâneos do Windows.
- `async_diagnostic`: processo filho não cooperativo só chama InitializeASI para obter READY. O diagnóstico externo confirma estado/PIN/contador zero, rejeita imagem divergente e processo lab como Hoodlum, e o filho termina normalmente.

Resultado final Debug/MSVC, Visual Studio 18 2026: **167/167 CTest Win32**, **117/117 x64**. São nove testes novos Win32; x64 valida regressões, não o novo worker/diagnóstico Win32. Logs: `build/alpha036-{win32,x64}/slice6-build.txt`, `slice6-ctest.txt`, `slice6-ctest-detail.log`.

Comandos: `cmake --build build/alpha036-win32 --config Debug --parallel 4`, `ctest --test-dir build/alpha036-win32 -C Debug --output-on-failure` e equivalentes `alpha036-x64`. CMake regenerou as configurações existentes Visual Studio 18 2026 para Win32/x64.

Evidência real do diagnóstico: `PID=14584 state=READY PIN=1 bootstrapTID=16900 count=0 callbackCapable=0 installed=0`. Estes são IDs do host descartável; nenhum dado novo do GTA foi coletado. Os testes não invocam InitializeASI sob loader lock do UAL: a segurança de partida do worker apoia-se no contrato Windows, enquanto o comportamento físico do loader fica para o smoke autorizado.

## Diagnóstico somente leitura

Binário: `build/alpha036-win32/client/windows/Debug/grandmp_native_counter_diagnostic.exe`.

Exige PID explícito e arquivo experimental esperado. Solicita somente PROCESS_VM_READ e PROCESS_QUERY_LIMITED_INFORMATION; enumera módulos e lê DATA/contador, sem attach de debugger, execução remota ou escrita. Confirma fingerprint Hoodlum, PE32/base, bytes/hash da imagem experimental, DATA export, snapshots estáveis e invariantes do bootstrap. Falhas/ambiguidades são INCONCLUSIVO; estado ainda não READY retorna código 4, identificação/leitura inconsistente 3, READY de residência confirmado 0. A flag PIN é informação do módulo admitido, sustentada pelo caminho/testes de aquisição; não existe consulta independente de refcount interno pelo utilitário.

Comando **para futura observação, após autorização de instalação/carregamento pelo usuário**; substituir 1234 pelo PID real:

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\build\alpha036-win32\client\windows\Debug\grandmp_native_counter_diagnostic.exe' --pid 1234 --expected-asi 'D:\Programacao\Workspace\Applications\grandmp\build\alpha036-win32\client\windows\Debug\GrandMPNativeCounterExperimental.asi'
```

`--lab-profile` existe apenas para o teste descartável e não deve ser usado no GTA. O utilitário não inicia o jogo. REQUESTED/INITIALIZING ou snapshot instável exigem nova observação; nunca implicam autorização para alterar estado do processo.

SHA-256 desta build experimental: `8DE9EDAC29035374AB37DC20312D5817E84335C10AFFC6E0B0984209EE38AF71`. Recompilar pode alterar o hash; o diagnóstico compara o arquivo esperado da build efetivamente usada com a cópia carregada.

## Próximo smoke, condicionado à autorização

Somente `D:\Jogos\GrandMP-Smoke`, Hoodlum homologado e UAL 7.7.0/configuração conhecida; jogo fechado antes de instalar manualmente o experimental autorizado, preservando GrandMP.asi homologado, Windowed Mode desativado e inventário conhecido de ASIs. Não foi executada instalação nesta tarefa.

O usuário deverá iniciar o GTA a partir de um ambiente que contenha o opt-in `GRANDMP_NATIVE_COUNTER_ASYNC_READY=1`. Depois observa o PID com o diagnóstico acima: READY/PIN, worker TID, count=0, callbackCapable=0 e installed=0 em menu e partida; encerra normalmente. Falha/FAILED/instabilidade persistente aborta o experimento: não chamar PrepareDeferred, não aplicar patch ou buscar engine. Retirar/desabilitar experimental somente com processo encerrado. Limpar o opt-in após a sessão.

A evidência nova será READY/PIN do ASI sem caller cooperativo na instalação real, com jogo funcional e encerramento normal. Não é repetição de medição LoaderLock, nem homologação de gameProcessEvent. Não há writer GTA, política de término forçado ou procedimento de patch nesta slice.

Windows native e reviewer revisaram lifetime, separação de capacidades e código somente leitura. Os testes foram executados pelo agente principal. A investigação anterior de LoaderLock permanece encerrada; wrapper físico, engine-thread e entidades continuam sem homologação.

Baseline `af3af1c90c61216dfc153a2f51b14f2368e2c8bb` preservada, índice vazio, sem commits. Diffs acumulados permanecem; arquivos counter/preflight/CMake compartilhados exigem hunks por slice quando commits forem autorizados. Novos diagnóstico/teste/relatório são exclusivos da Slice 6.
