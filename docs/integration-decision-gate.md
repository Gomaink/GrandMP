# Alpha 0.2.9 — decisão de integração Hoodlum

## Baseline Git proposta

No início deste gate, o repositório tinha zero commits e 62 arquivos não rastreados. O inventário foi conferido por caminho e tamanho: `.codex/` (7), `agent-definitions/` (6), `client/` (14), `docs/` (9), `shared/` (7), `tests/` (14) e cinco arquivos isolados (`AGENTS.md`, `.gitignore`, `CMakeLists.txt`, `README.md`, `server/README.md`). A proposta de primeiro commit é `chore(repo): establish GrandMP foundation through alpha 0.2.8`, incluindo somente esses 62 arquivos e as correções documentais deste gate após revisão. Nenhum commit, `git add`, push ou PR foi executado.

`.gitignore` exclui `/build/`, `/out/`, diretórios de IDE/CMake, executáveis, DLLs, ASIs, bibliotecas, objetos, logs, `.env` e `*.local`. Os checkouts locais de MinHook e plugin-sdk, builds e binários estão sob `build/`; `git check-ignore` confirmou esses caminhos. Não há binários, assets proprietários do GTA ou arquivos de segredo detectados entre os 62 arquivos elegíveis. Os arquivos de teste referenciam MinHook por checkout externo: nenhum código terceiro foi incorporado. Qualquer distribuição futura do MinHook exigirá preservar sua licença BSD de duas cláusulas e os avisos HDE32/HDE64. O inventário é uma proposta para revisão; o novo relatório acrescenta um arquivo além dos 62 iniciais.

## Contexto de `InitializeASI` no Ultimate ASI Loader 7.7.0

Na [tag v7.7.0, `source/dllmain.cpp`](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/v7.7.0/source/dllmain.cpp), `DllMain` chama `Init` (2456–2462). `Init` lê os INIs e `DontLoadFromDllMain` com padrão verdadeiro (2342–2355). Com essa opção, instala wrappers na IAT (2395–2454). Um wrapper chama `LoadPluginsAndRestoreIAT`, que restaura a IAT e chama `LoadEverything` (756–792). `LoadEverything` executa uma vez e chama `LoadPlugins` (744–754). `FindFiles` carrega cada ASI via `LoadLib`/`LoadLibraryW`, obtém `InitializeASI` por `GetProcAddress` e o chama sincronicamente (605–657). A tag não oferece outro ponto diferido com garantia documentada de estar fora do loader lock. `LoadFromAPI` apenas seleciona o wrapper que dispara a carga.

**Inferência, não comportamento observado:** um wrapper de API pode ser alcançado durante o `DllMain` de outro módulo; o fonte não testa se o loader lock está livre. Portanto, `DontLoadFromDllMain=1` não demonstra sozinho que o `InitializeASI` concreto foi invocado fora desse lock. A [Microsoft documenta que `DllMain` roda sob loader lock](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices). A coincidência entre TID de `InitializeASI` e TID da janela no smoke anterior também não prova ausência do lock.

O binário instalado foi lido sem executar o jogo: `D:\Jogos\GrandMP-Smoke\dinput8.dll`, 1.699.840 bytes, VersionInfo 7.7.0, SHA-256 `29FEE0AA46414864AD1F65B2689B470BFB2CF2F09A94E2AFD271E68F2E16743B`. `global.ini` e `scripts/global.ini` contêm `DontLoadFromDllMain=1`; os demais INIs dessa sequência não foram encontrados. Esse fingerprint não foi comparado ao asset oficial da release. O smoke anterior confirmou carga do ASI e execução da exportação, não a pilha ou o estado do loader lock. **Conclusão: não há ainda ponto comprovadamente seguro para iniciar runtime residente.**

## `CGame::Process` no Hoodlum

O executável local mantém o SHA-256 Hoodlum `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`. Desassemblagem x86: `0x53BEE0: 83 EC 0C 53 56 57` (`sub esp,0Ch; push ebx; push esi; push edi`); o próximo CALL vai a `0x541DD0`. `0x53E981: E8 5A D5 FF FF` chama diretamente `0x53BEE0` sem argumentos empilhados para a função. O epílogo em `0x53C234` restaura registradores/pilha e segue por salto de cauda a `0x6EB710`. A assinatura `void()`/cdecl do [plugin-sdk, revisão `15f15b60bbf74c106e1b496ff92c98764abf4605`](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/meta/meta.CGame.h) é coerente com a chamada observada, mas a referência publicada é rotulada **Compact**.

O [MinHook v1.3.4, `trampoline.c`](https://github.com/TsudaKageyu/minhook/blob/v1.3.4/src/trampoline.c) termina a cópia quando cobriu o JMP x86 de cinco bytes. Nesse prólogo, copiaria três instruções inteiras (`83 EC 0C 53 56`) e retornaria a `0x53BEE5` para executar o `push edi` original. Isso demonstra viabilidade estrutural local do prólogo, não a ABI completa. Outros chamadores, recursão indireta e fase de execução durante menu, loading, partida e encerramento não foram comprovados. A chamada observada pertence ao processamento de frame, sem prova de disponibilidade do jogador. Não há autorização para chamar `FindPlayerPed` ou `FindPlayerCoors`.

## MinHook v1.3.4 e política conservadora

O protótipo isolado da Alpha 0.2.8 permanece válido para função própria com ABI conhecida; não exercitou instalação ou remoção com chamadas concorrentes. Na revisão [`c3fcafdc10146beb5919319d0683e44e3c30d537`](https://github.com/TsudaKageyu/minhook/tree/c3fcafdc10146beb5919319d0683e44e3c30d537), [`hook.c`](https://github.com/TsudaKageyu/minhook/blob/v1.3.4/src/hook.c) mostra que `Freeze` pode retornar sucesso após falha individual de `OpenThread` ou `SuspendThread`; `ProcessThreadIPs` ignora falhas de `GetThreadContext` e `SetThreadContext`. `Unfreeze` reabre handles e não propaga falha de `ResumeThread`. `EnableHookLL` escreve JMP sem comparar novamente os bytes de propriedade, e não valida os retornos da restauração de `VirtualProtect` e de `FlushInstructionCache`. `MH_RemoveHook` libera o trampoline sem demonstrar ausência de chamadas em trânsito. A enumeração de threads não exclui uma thread criada durante a instalação.

**Decisão:** MinHook v1.3.4 sem alterações não cumpre nossa política de falha conservadora. Propagar uma falha de suspensão seria uma alteração pequena no código, mas insuficiente como solução: ainda faltariam retenção de handles, tratamento de EIP e retomada, quiescência de threads, comparação de propriedade imediatamente antes da escrita, e duração de callback/trampoline. Uma variante corrigida requer revisão e validação isolada próprias; não foi integrada neste slice. O evento `gameProcessEvent` do plugin-sdk altera o CALL interno e não resolve propriedade e restauração do patch.

## Matriz de decisão

| Critério | Estado | Evidência mínima para GO |
| --- | --- | --- |
| Inicialização fora do loader lock | **NO-GO** | Traço da chamada concreta no loader instalado mostrando pilha/contexto após inicialização de DLL, ou ponto diferido com contrato verificável. |
| Residência do módulo | **NO-GO** | Após entrada segura, PIN do próprio módulo validado e runtime de duração de processo ligado ao ASI; sem referência a objeto local de `InitializeASI`. |
| ABI de `CGame::Process` | **NO-GO** | Revisão de todos os chamadores e caminhos de retorno/recursão relevantes no Hoodlum, mais contexto de execução. |
| Instalação segura do detour | **NO-GO** | Falha conservadora para suspensão/contexto/retomada, quiescência e propriedade dos bytes demonstradas em ensaio isolado, inclusive falhas injetadas. |
| Fluxo original preservado | **Parcial** | Trampoline e chamada da original demonstrados no protótipo próprio; ainda falta validação do alvo Hoodlum e de recursão. |
| Duração de trampoline e callback | **NO-GO** | Callback/estado residentes e estratégia que exclua execução em trânsito antes de remover/liberar trampoline; sem restauração em `DllMain`. |
| Recuperação diante de falhas | **NO-GO** | Contrato e testes de falha parcial, conflito com terceiro e erro de proteção/cache, sem deixar patch parcial ou liberar código alcançável. |

Como a matriz contém bloqueios, **não há especificação de hook experimental autorizada**. O próximo experimento, somente após todos os gates, seria contar chamadas de `CGame::Process` e sempre encaminhar à original, sem acessar o jogador. O `GrandMP.asi` não foi alterado, nenhum hook foi instalado, e o GTA não foi executado nesta etapa.
