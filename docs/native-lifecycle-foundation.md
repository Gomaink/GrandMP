# Alpha 0.3.5 — Native Lifecycle Foundation (Slice 1)

## Escopo e isolamento

Este slice acrescenta um controlador de ciclo de vida e um host Win32 descartável para simular atualizações. O código fica em `client/windows/` e em uma biblioteca CMake própria, `grandmp_windows_lifecycle`; **não é ligado a `GrandMP.asi`**, não altera `asi_entry.cpp`, não instala hooks e não acessa o GTA.

O `DllMain` do ASI continua mínimo. O `InitializeASI` existente ainda cria um runtime local e não é considerado fora do loader lock. O controlador não é chamado por nenhuma exportação do ASI. `requestInitialization()` apenas enfileira a intenção; um host chama `runDeferredInitialization()` num ponto de inicialização que ele próprio considera pronto. Nenhuma thread é criada para tentar adivinhar quando o loader lock foi liberado.

## Estados e transições

`CallbackLifecycleController` é criado como `shared_ptr` e implementa:

```text
cold -> initialization_queued -> initializing -> ready
                         \             \          \
                          \             +----------+ -> stopping -> stopped
                           +-> stopping
                                      initializing -> failed -> stopping
```

- A inicialização só pode ser solicitada uma vez. `registerCallback()` aceita callbacks durante setup (staged) ou em `ready`, e retorna um ID para remoção. Dispatch antes de `ready` não invoca callback.
- Setup executa fora do mutex. Em falha, cancelamento ou exceção, executa rollback e não publica o estado `ready`.
- Shutdown primeiro fecha a admissão. `requestShutdown()` não espera, podendo ser chamado pelo próprio callback.
- `unregisterCallback()` remove o callback da lista antes de aguardar o corpo dos invocations já admitidos. Chamado de dentro de callback, fecha a admissão e retorna `would_deadlock`; a chamada atual mantém o slot vivo até retornar. O retorno `completed` **não permite descarregar a DLL**: snapshots e a própria chamada externa a `dispatch()` ainda precisam retornar ao host.
- `waitForCallbacks()` e `waitForShutdown()` recusam espera reentrante (`would_deadlock`).
- O host precisa parar/desregistrar a fonte externa e **juntar todas as chamadas de dispatch até retornarem ao host**, só então chama `confirmSourceQuiescent()`.
- `waitForShutdown()` só finaliza depois dessa confirmação e da drenagem; executa cleanup fora do lock e libera os callables. Se cleanup falha, unload continua inelegível.
- Dispatches concorrentes são serializados. Reentrada na mesma thread é rejeitada. Cada dispatch mantém uma referência forte ao controller enquanto executa e snapshots compartilhados dos slots; callbacks rodam fora do mutex de estado.

`unloadSafe()` só fica verdadeiro em `stopped`, sem callbacks em voo, depois da confirmação externa de quiescência e com os slots/cleanup removidos. Isso é uma barreira de política para um owner cooperativo. Não é uma trava do loader: não impede outro componente de chamar `FreeLibrary` diretamente, não verifica se a fonte mentiu e não protege código que contorne o controller. O protótipo não oferece garantia de unload do módulo no nível do sistema operacional.

## Host e testes

O executável `grandmp_callback_lifecycle_host.exe` é Win32 independente. Ele solicita a inicialização, chama o ponto diferido depois de configurar seu loop simulado, envia N ticks, para a fonte, confirma quiescência e encerra. Exemplo local:

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\build\alpha035-win32\client\windows\Debug\grandmp_callback_lifecycle_host.exe' --ticks 16
```

Saída esperada: `ticks=16 callbacks=16 state=stopped unload_safe=1`.

Os testes CTest exercitam inicialização única, registro durante setup, dispatch antes/depois da inicialização, falha parcial com rollback, registro concorrente a dispatch, remoção concorrente aguardando callback em voo, reentrada, shutdown pedido pelo callback, shutdown externo enquanto callback está bloqueado, shutdown concorrente com inicialização, cleanup que falha e recusa de shutdown sem quiescência.

O gate verifica a política do controller e a duração do callable: a remoção espera o callback ativo terminar. **Os testes não descarregam código de DLL em execução, não testam o ASI Loader e não provam residência física do módulo.** A referência forte protege a instância C++ do controller durante uma chamada; não fixa a imagem que contém as instruções do método/callback.

## Residência e limites

Uma instância com callback publicado precisa manter callback e estado vivos até a fonte ser desregistrada e drenada. Se a fonte não puder provar isso, a política segura futura é manter o módulo residente até o encerramento do processo e desativar o trabalho logicamente. O suporte atual a `GetModuleHandleExW(...PIN)` em `ModuleResidency` não foi acionado nem conectado a este controller: PIN é irreversível durante a execução, não resolve loader lock e não torna patch/dispatch atômicos.

O plugin-sdk `Events::Remove()` não é usado nem tratado como restauração do CALL `0x53E981`. O host não modela `Add`/patch real, concorrência com instruções do GTA, conflitos entre ASIs, loader lock, afinidade com `CGame::Process` ou comportamento no encerramento do GTA. O contrato de callback também não chama `RemotePlayerAdapter`, backend, logger, API do jogo ou operações de alocação como requisito de um futuro contador.

## Decisão para o próximo experimento

**GO somente para o contrato do host sintético; NO-GO para callback no GTA.** Antes de um ensaio nativo, continuam necessários: um ponto de inicialização demonstravelmente fora do loader lock; residência explícita; fonte de callback que permita desinscrever e drenar chamadas; instalação de patch com propriedade e quiescência verificáveis; e aprovação separada para ASI experimental.

O menor experimento posterior, após esses gates e autorização, é um ASI descartável e opt-in que apenas incrementa um contador atômico e encaminha a função original uma vez. A validação física observaria contagem, thread e encerramento normal em instalação isolada, sem `RemotePlayerAdapter`. Qualquer falha de identidade, residência, quiescência, encaminhamento ou encerramento é NO-GO. Este documento não autoriza nem inicia esse ensaio.

O laboratório subsequente de DLL real, sem GTA, está descrito em [`native-dll-lifetime-lab.md`](native-dll-lifetime-lab.md). Ele verifica somente o contrato cooperativo do host com `LoadLibrary`/`FreeLibrary` e não altera os gates acima.
