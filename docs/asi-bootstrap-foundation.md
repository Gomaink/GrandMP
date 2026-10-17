# Alpha 0.3.5 — ASI Bootstrap Foundation (Slice 3)

## Escopo e artefatos

Este slice acrescenta uma máquina de estados pequena e um módulo ASI experimental separado. O alvo `GrandMPBootstrapExperimental.asi` só é gerado no build Win32. Ele não é `GrandMP.asi`, não está ligado ao alvo homologado e não foi copiado para a instalação de smoke. Não contém hooks, patches, chamadas ao jogo, acesso a entidades, rede ou dependência do `RemotePlayerAdapter`.

`DllMain` apenas guarda o `HMODULE`. `InitializeASI` faz uma transição atômica `cold -> entry_seen` e retorna. Não aloca, calcula hashes, escreve arquivos, cria threads, fixa o módulo nem inicializa trabalho do motor. Um export separado, `GrandMPBootstrapRunDeferred`, só avança a partir de `entry_seen`; seu chamador precisa fornecer um ponto seguro. Após a transição, o export adquire uma referência própria com `GetModuleHandleExW(FROM_ADDRESS)` e a conserva sem liberar até o encerramento do processo. O export `GrandMPBootstrapDisableLogically` apenas muda `resident -> logically_disabled`; ele não remove a referência.

Estados: `cold`, `entry_seen`, `initializing`, `resident`, `failed` e `logically_disabled`. Uma falha na aquisição resulta em `failed` e não permite nova tentativa. O módulo não publica callbacks neste slice. Não existe export de unload nem limpeza em `DLL_PROCESS_DETACH`.

## Ultimate ASI Loader 7.7.0

Referência consultada: release/tag oficial [v7.7.0](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v7.7.0), fonte [`source/dllmain.cpp`](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/v7.7.0/source/dllmain.cpp). A release identifica a revisão como `38c04fd`; o hash exato do binário usado no smoke anterior está registrado em `runtime-evidence-windbg.md`. Não foi copiado código do loader.

Observações diretas do código dessa tag:

- O `DllMain(DLL_PROCESS_ATTACH)` do loader chama `Init()` sincronicamente (`DllMain`, próximo às linhas 2456–2462).
- `Init()` lê `DontLoadFromDllMain`, com padrão `TRUE` (`Init`, linhas 2342–2355). Com a opção falsa e sem `ForceEntryPointHook`, chama `LoadEverything()` diretamente durante esse caminho de `DllMain` (`Init`, 2395–2454).
- Com a opção verdadeira, `Init()` instala wrappers de API na IAT. Um wrapper pode chamar `LoadPluginsAndRestoreIAT`, que restaura essa IAT e chama `LoadEverything()` sincronamente. A configuração adia a carga imediata feita por `Init`; o código não declara que a invocação posterior ocorrerá fora do loader lock.
- `FindFiles()` chama `LoadLibraryW` para cada `.asi`, resolve `InitializeASI` via `GetProcAddress` e chama a exportação diretamente, na mesma thread (`FindFiles`, 605–657). `LoadPlugins()` enumera a raiz e pastas de plugins configuradas.
- Nesse caminho, não há `FreeLibrary` do handle retornado para cada ASI. Isso mostra a referência mantida pelo fluxo normal, mas não exclui descarregamento pedido por outro componente. O tratamento de `FreeLibrary` do loader protege o próprio módulo do loader, não estabelece contrato geral de hot-unload para plugins.

**Inferência delimitada:** como um wrapper pode ser chamado enquanto código de outro `DllMain` ainda está em execução, não se pode inferir loader-lock livre apenas de `DontLoadFromDllMain=1`, da thread observada ou da ausência de frames `Ldr`. O código não oferece ao plugin uma confirmação verificável desse estado. A chamada física de `InitializeASI` anteriormente registrada e a coincidência de TID com a janela continuam sendo observações, não prova de lock livre.

Um probe opt-in com o loader real está preparado em `scripts/Test-UalSyntheticHost.ps1`. Ele exige o SHA-256 do binário x86 7.7.0 previamente observado, copia-o para uma pasta nova dentro de `build/runtime-evidence`, coloca ali somente um executável host e um ASI sentinela próprio, configura `DontLoadFromDllMain=1` e dispara `GetStartupInfoW` no host. O subprocesso tem timeout e não abre o GTA. O resultado esperado é o marcador criado pelo `InitializeASI` sentinela. O probe não é executado por CTest automaticamente, pois carrega um proxy de terceiros; deve ser invocado separadamente pelo operador quando desejar validar esse caminho específico. O subprocesso e sua pasta de trabalho **não são uma sandbox**: o loader roda com as permissões do usuário e ainda pode interagir com recursos fora dessa pasta. O script emite esse aviso antes de executar. Mesmo um PASS só prova o carregamento síncrono nesse host, não ausência universal de loader lock no GTA.

Execução feita em 2026-10-12 com `D:\Jogos\GrandMP-Smoke\dinput8.dll`: PASS; hash `29FEE0AA46414864AD1F65B2689B470BFB2CF2F09A94E2AFD271E68F2E16743B`; o host registrou `GetStartupInfoW` e o sentinela criou o marcador (PID 17836, TID 20436). Arquivos preservados em `build/runtime-evidence/ual-synthetic-20261012-234600-243/`. Essa execução observou o wrapper escolhido após `LoadLibraryW` no subprocesso sintético. **Não mediu nem provou loader-lock livre**, nem representa chamada feita a partir de qualquer outro chamador/contexto.

O host incluído no CTest testa o contrato do módulo experimental diretamente depois de `LoadLibraryW` retornar.

## O que os testes demonstram

Os testes da máquina de estados exercitam solicitação concorrente, execução diferida única, falha parcial, estado residente e desligamento lógico. O host Win32 carrega o ASI experimental com `LoadLibraryW`, chama `InitializeASI` em múltiplas threads, chama o ponto diferido depois do retorno de `LoadLibraryW`, valida exatamente um inicializador, desliga logicamente e libera apenas sua própria referência. Em seguida, comprova que o módulo continua enumerável no processo pela referência própria retida. Cada cenário executa em um processo descartável do CTest; o encerramento normal do processo é a liberação final feita pelo Windows.

Isso comprova o comportamento da máquina de estados, a aquisição de referência própria e a retenção sob um owner cooperativo. **Não** comprova loader lock do Ultimate ASI Loader, thread-affinity do GTA, residência contra chamada hostil/externa de unload de referências indevidas, ou que exista no GTA um ponto que possa chamar `GrandMPBootstrapRunDeferred` com segurança. Também não prova nada sobre patch, callback ou entidade do jogo.

## Patch do call-site e compatibilidade

O call-site `0x53E981` não é tocado. A futura propriedade exclusiva do patch continua sem solução demonstrada: bytes esperados detectariam somente uma diferença já ocorrida, não corrida entre checagem e escrita, alteração simultânea, dispatch em andamento nem cadeia de ownership de outro ASI. `Events::Remove()` segue sem equivaler a restaurar a instrução. Inventariar módulos/plugins conhecidos poderia detectar algumas combinações incompatíveis, mas não prova ausência de plugins que alterem o mesmo endereço. Até existir mecanismo de instalação transacional/quiescente e política de convivência verificável, o resultado continua **NO-GO**.

## Resultado e próxima evidência

O bootstrap experimental está utilizável por um host que possa chamar explicitamente o ponto diferido após uma inicialização segura. No Ultimate ASI Loader/GTA, ainda não há executor diferido provado para consumir a solicitação sem confiar no contexto de `InitializeASI`; portanto o runtime nativo permanece inerte e nenhum callback é autorizado.

Antes de qualquer uso no GTA, é necessário demonstrar em execução um caminho de entrada após retorno do loader lock, com política de módulo residente e sem trabalho de motor nesse ponto. Depois, ainda será necessário resolver separadamente exclusividade/quiescência do patch e a duração do callback. Este slice não autoriza teste físico nem instalação do arquivo experimental.
