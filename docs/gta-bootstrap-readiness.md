# Alpha 0.3.5 — GTA Bootstrap Readiness (Slice 4)

## Decisão

**LoaderLock observado livre na captura física `20261013-002726`, na entrada de `GrandMP!InitializeASI`.** O relatório foi corrigido por reprocessamento offline do log existente, conforme registro ao final. Esta pendência pontual está encerrada; o resultado não autoriza inicialização nativa, hooks/callbacks ou integração com o motor. O candidato estudado continua sendo o caminho direto do Ultimate ASI Loader 7.7.0: seu wrapper diferido chama `InitializeASI` sincronicamente, na thread que disparou a API, mas não oferece um contrato universal de loader lock livre.

Essa medição, mesmo positiva, não certifica outras configurações/chamadores e não prova que a thread seja um contexto seguro de atualização do motor. Portanto, ela poderia liberar somente o estudo de inicialização leve/residência nessa entrada específica; não autoriza eventos, hooks, patches, APIs internas ou entidades.

## Revisão do estado

- O `GrandMP.asi` atual executa diagnóstico síncrono em `InitializeASI`. O contexto de loader lock permanece desconhecido. A mudança do Slice 3 está isolada em `GrandMPBootstrapExperimental.asi`; sua `InitializeASI` experimental apenas faz uma transição atômica. O export `GrandMPBootstrapRunDeferred` exige chamada explícita e adquire uma referência própria ao módulo; o loader não o chama.
- Os logs físicos anteriores estão em `build/runtime-evidence/20261012-223706/`. Em `01-initialize-asi.txt`, o breakpoint parou em `GrandMP!InitializeASI`, PID `0x5694`, TID `0x49B8`, com frames do `DINPUT8.dll` e do jogo na stack. `!ntsdexts.locks` retornou apenas `Scanned 13 critical sections`: não associou seção, endereço, owner ou recursão ao loader lock. `FastPebLock` aparece em dados da stack, mas não identifica o loader lock.
- As capturas `02-menu-callsite.txt` e `03-gameplay-callsite.txt` observam `0x53E981` na mesma TID e bytes `E8 5A D5 FF FF`, chegando a `0x53BEE0`. A fase da captura rotulada menu é ambígua; `t` falhou com implicit wait após a transferência. Isso não demonstra um executor seguro ou ciclo completo.
- O probe sintético do UAL demonstrou somente que, em um host controlado, uma chamada a `GetStartupInfoW` após `LoadLibraryW` pode acionar a enumeração e a chamada síncrona do sentinela. Não mede o PEB/loader lock do GTA.

## Avaliação dos mecanismos

| Candidato | Loader lock / afinidade | Residência / encerramento | Decisão |
|---|---|---|---|
| UAL 7.7.0 → wrapper de API → `InitializeASI` | Chama export síncrona na thread do wrapper. `DontLoadFromDllMain=1` adia a carga imediata, mas o código não promete lock livre para todos os chamadores. A TID observada não é prova de afinidade de motor. | O caminho de `LoadLibrary` do UAL mantém a referência normal em seu fluxo observado, mas não impede um terceiro de descarregar. A fundação experimental pode obter referência própria somente num ponto diferido explicitamente invocado. | **Prioridade para diagnóstico passivo**, única opção que não introduz outra thread/callback. GO condicional apenas para bootstrap leve nessa chamada se a medição direta passar e as condições de loader/configuração forem fixadas. |
| Worker/threadpool iniciada em `InitializeASI` | Pode começar enquanto um loader lock externo ainda está adquirido; executa em thread arbitrária e não no ciclo do jogo. | Exigiria referência própria antes de iniciar trabalho; shutdown/join não prova afinidade. | Rejeitada. |
| APC ou fila de trabalho do sistema | APC depende de estado alertable e não dá thread de engine; work item roda em thread arbitrária. | Mais coordenação de cancelamento e residência, sem melhorar contexto. | Rejeitada. |
| `SetTimer`/`TimerProc` ou mensagem da janela | Depende de pump e despacho; callback pode ocorrer na thread da janela, mas isso não a identifica como thread de atualização do jogo. A instalação do timer ainda teria de ocorrer num ponto seguro. | `KillTimer` não drena callback/mensagem pendente; DLL e estado precisariam continuar residentes. | Não escolhido; host sintético anterior não demonstra despacho no Hoodlum. |
| plugin-sdk `Events::gameProcessEvent` | Tem o contexto de ciclo de jogo mais promissor, mas instalar o primeiro handler altera o CALL `0x53E981`. | Não há ownership/quiescência, instalação concorrente ou remoção segura demonstrados. `Remove()` não restaura o patch. | Continua NO-GO; circular como bootstrap porque registrar o evento já requer integração nativa. |

## Roteiro da medição passiva — executado em 20261013-002726

### Captura automatizada específica de LoaderLock

O launcher existente agora oferece `-LoaderLockOnly`: gera somente quatro scripts, captura uma ocorrência de `InitializeASI`, destaca o depurador e encerra a investigação. Não executa as etapas de menu, partida ou `CGame::Process`. Não usa o ASI experimental. O comando abaixo **inicia o GTA sob CDB somente após confirmação `SIM`** e deve ser executado pelo usuário depois de autorizar esta medição:

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\scripts\Invoke-RuntimeEvidence.ps1' -LoaderLockOnly
```

O jogo deve estar fechado antes do comando; o Windowed Mode continua desativado. O launcher preserva os checks de CDB x86/CLI, hashes de Hoodlum/ASI/UAL, arquitetura, ImageBase, PDB local e ausência de ASIs desconhecidos. Não recompila ou copia arquivos. Usa o cache Microsoft de símbolos e acrescenta o diretório do PDB GrandMP em linhas próprias, com `SYMOPT_EXACT_SYMBOLS` e sem `SYMOPT_LOAD_ANYTHING`.

Na carga do módulo, resolve a exportação e arma `ba e 1 /1`. Na entrada coleta PID/TID/EIP, registradores, stacks, stack bruta, lista de módulos, versão do Windows, `lmvm`/`!lmi ntdll` com GUID/age do PDB e estas consultas:

```text
dt ntdll!_PEB @$peb LoaderLock
?? &((ntdll!_PEB*)@$peb)->LoaderLock
?? ((ntdll!_PEB*)@$peb)->LoaderLock
```

O último comando exibe o objeto usando **o tipo nativo do membro fornecido pelo PDB**, sem converter o ponteiro para uma estrutura presumida. Quando o membro é `RTL_CRITICAL_SECTION*`, a saída inclui `OwningThread`, `RecursionCount`, `LockCount`, `LockSemaphore`, `SpinCount` e `DebugInfo`. Se vier `Void`, não houver tipo, a leitura falhar ou o formato não for reconhecido, o relatório será `INCONCLUSIVO`; não se tenta offset fixo, cast do objeto ou `!critsec` sobre tipo desconhecido. O endereço de armazenamento do campo deve coincidir tanto com a expressão tipada `&campo` quanto com PEB + offset **obtido de `dt` naquele PDB**, e o endereço do objeto deve coincidir nas duas consultas independentes.

Arquivos em `build/runtime-evidence/<data-hora>/`:

- `01-loader-lock.txt`: evidência bruta, incluindo o detach confirmado.
- `loader-lock-result.json`: interpretação conservadora e dados de PID/TID, campo, objeto, owner e contagens quando disponíveis.
- `installation.json`: caminhos e fingerprints conferidos antes da execução.
- Quatro `.wds`: boot, carga do módulo, captura do lock e cleanup inconclusivo; todos com CRLF e comandos em linhas próprias.

`OBSERVADO_LIVRE` exige objeto tipado, owner nulo, recursão zero e LockCount=-1. `OBSERVADO_OCUPADO` exige owner e recursão não nulos e contagem compatível; informa também se o owner é a TID capturada. Contagens contraditórias, PDB não confirmado, EIP inesperado, captura incompleta ou ausência de detach são `INCONCLUSIVO`. A automação limpa breakpoints, executa `.detach /h` no evento de hardware identificado, fecha o log e sai com `q`. Nenhuma exceção real recebe tratamento genérico.

Se a exportação não resolver, a sessão destaca sem armar breakpoint. Se o GTA encerrar antes da captura, `epr` encerra o CDB e o relatório fica inconclusivo. Se o módulo/entrada não for atingido enquanto o jogo continuar aberto, o CDB aguarda: encerre o jogo normalmente para finalizar. Se houver uma exceção inesperada, preserve o código/log e não use `.detach /h` para marcá-la como tratada. Não mate o CDB à força. `-CheckOnly` verifica somente disponibilidade/CLI do debugger, sem iniciar ou anexar ao GTA:

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\scripts\Invoke-RuntimeEvidence.ps1' -LoaderLockOnly -CheckOnly
```

A preparação foi validada com processos Win32 descartáveis; os resultados estão registrados ao final deste documento. **A medição física foi executada pelo usuário em `20261013-002726`.** O roteiro de captura acima é referência histórica: não repetir o GTA para corrigir o relatório. O log e JSON originais permanecem preservados; o resultado correto está no relatório reprocessado descrito abaixo. A análise encerra esta pendência, sem liberar hooks, callbacks ou integração com o motor.

### Referência manual

O resultado deve trazer evidência nova do lock no instante da entrada. A medição usa breakpoint de hardware temporário (`ba e 1 /1`) na exportação existente e não escreve na memória do GTA. O breakpoint altera temporariamente registradores de depuração (DRx) e pausa o processo. Não instalar `GrandMPBootstrapExperimental.asi`, alterar INIs, editar memória, executar `.call`, chamar APIs do alvo nem usar stepping `t`.

1. O usuário inicia manualmente o GTA na instalação isolada com a configuração e o `GrandMP.asi` homologado que já estão presentes. Não copiar/substituir arquivos. Crie uma pasta nova `build/runtime-evidence/bootstrap-lock-<data-hora>` antes de abrir o debugger. No WinDbg x86, use **File > Start debugging > Launch executable (advanced)** com Executable `D:\Jogos\GrandMP-Smoke\gta_sa.exe`, Start directory `D:\Jogos\GrandMP-Smoke`, sem argumentos; não use o launcher antigo que se encerra automaticamente. Na parada inicial do processo, abra o log nessa pasta, configure símbolos e pare no carregamento do ASI (substitua o marcador pelo caminho absoluto real):

```text
.logopen /t D:\Programacao\Workspace\Applications\grandmp\build\runtime-evidence\bootstrap-lock-<data-hora>\initialize.txt
.symfix
.sympath+ D:\Programacao\Workspace\Applications\grandmp\build\alpha027-win32\client\windows\Debug
!sym noisy
.reload /f ntdll.dll
sxe ld:GrandMP
g
```

   Quando `GrandMP.asi` carregar, confirme caminho, símbolos/PDB e export. **Somente então** arme o breakpoint one-shot de hardware; não use `bu`, `bp` ou `bm`, que podem inserir breakpoint de software/escrever no processo:

```text
lmv m GrandMP
!lmi GrandMP
x GrandMP!InitializeASI
ba e 1 /1 GrandMP!InitializeASI
bl
g
```

   Se o módulo ou export não for resolvido, pare e registre a captura como inconclusiva. `ba` utiliza registradores de depuração de hardware (DRx) e pausa brevemente o processo, embora não reescreva bytes do GTA. A saída de `!sym noisy`, `.reload` e `!lmi` deve ser preservada para confirmar a identidade/compatibilidade dos símbolos e PDB; não confie em nomes/linhas sem correspondência. [`!lmi`](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/-lmi), [símbolos WinDbg](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/getting-started-with-windbg), [status de símbolos](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/symbol-status-abbreviations).
2. Na parada da primeira instrução de `InitializeASI`, o log aberto no passo anterior continua ativo. Execute estes comandos, uma linha por vez; **não** repita `.symfix`, pois isso redefiniria o caminho local do PDB GrandMP:

```text
.reload /f ntdll.dll
vertarget
lmvm ntdll
!peb
.printf "PID=%x TID=%x PEB=%p\n", @$tpid, @$tid, @$peb
dt ntdll!_PEB @$peb LoaderLock
x ntdll!*LoaderLock*
!ntsdexts.locks
```

3. Primeiro determine exatamente o que os símbolos mostram: endereço de armazenamento do membro `LoaderLock` dentro do PEB, valor armazenado nesse membro (se for ponteiro), e tipo do objeto apontado. **Não** passe o endereço do campo no PEB para `!critsec` por engano. Só se a informação de símbolos confirmar que o objeto em um endereço específico é uma `RTL_CRITICAL_SECTION`, passe o endereço desse objeto (ou o valor do ponteiro tipado que aponta para ele) a:

```text
!critsec <endereco-confirmado-pelos-simbolos>
```

Comparar `OwningThread` com o `TID` impresso e guardar `LockCount`, `RecursionCount`, tipo, endereço do campo, valor do ponteiro/objeto, versão de Windows e identidade/estado do PDB. Se os símbolos não distinguirem inequivocamente campo, tipo e objeto apontado, não executar `!critsec` e registrar **INCONCLUSIVO**. Não deduzir endereços por offsets constantes. A documentação pública do PEB marca seu layout como sujeito a mudanças e representa a maior parte dos campos internos como reservados; `dt` usa informação de símbolos/PDB, e símbolos públicos podem não incluir esse campo. [PEB](https://learn.microsoft.com/en-us/windows/win32/api/winternl/ns-winternl-peb), [`dt`](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/dt--display-type-), [`!critsec`](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/-critsec).

4. Se não houver campo/endereço/tipo resolvido, ou se o lock não for uma critical section com semântica conhecida, registrar **INCONCLUSIVO**, fechar o log e continuar normalmente. Não interpretar `!ntsdexts.locks` sem associação ao objeto do loader como lock livre. Se resolvido, registrar também `r`, `kv 100`, `dds @esp L40`, `lmv m dinput8`, `lmv m GrandMP`, `lmv m gta_sa` e a configuração efetiva do loader recolhida sem modificá-la. Os endereços crus de retorno devem ser mapeados ao módulo e tratados como offsets se não houver símbolos confiáveis; não inferir o nome da API wrapper sem call-site/símbolos suficientes. Fechar o log e usar `g` para continuar; deixar o jogo sair normalmente.

O debugger suspende o processo enquanto inspeciona. A parada temporária altera timing; o breakpoint de hardware não reescreve bytes do GTA. Se o comando PDB/símbolo não corresponder ao Windows observado, não contornar com leitura de memória em offset presumido. Remover breakpoint temporário/continuar ou destacar conforme o roteiro validado; não sair do processo pelo comando `qd`.

## Critério de decisão após a captura

| Resultado | Decisão |
|---|---|
| Símbolos compatíveis identificam inequivocamente o loader lock e mostram que está livre na entrada; captura também identifica o chamador/wrapper e configuração do UAL | **GO limitado** para desenhar, em revisão própria, inicialização leve na entrada UAL observada e residência explícita. Continua **NO-GO** para motor, callbacks, evento, patch e entidades. A observação vale para aquela combinação de Windows, binário/configuração do UAL e caminho de chamada; deve ser repetida se qualquer uma mudar. |
| Loader lock mostra owner atual ou outro owner / contagem bloqueada | **NO-GO** para executar inicialização síncrona em `InitializeASI`; manter somente solicitação atômica. |
| Campo ou tipo não disponível, symbols mismatch, resultado conflitante ou stack/caller não identificável | **NO-GO / inconclusivo**; não tentar offset fixo. A evidência física mínima ausente é uma visualização confiável do objeto e do estado de aquisição do LoaderLock no breakpoint. |

Um resultado “livre” não é contrato universal da exportação: UAL permite selecionar wrappers/configuração de trigger, e um chamador diferente pode chegar ao mesmo código sob contexto diferente. Antes de alterar `InitializeASI`, o runtime experimental teria de rejeitar combinações diferentes de loader/configuração ou continuar inerte. Nenhuma medição substitui confirmação de thread de atualização para operações do motor.

## Residência e saída

Nenhum teste neste slice descarrega a DLL. O bootstrap existente só retém uma referência normal quando a chamada diferida explícita é feita; não cria thread nem a chama desde `InitializeASI`. Para o próximo experimento, a política continua sendo manter módulo/código/estado residentes até o processo terminar. Desativação será lógica. Não executar `FreeLibrary`, hot-unload, `Events::Remove()` como restauração ou alteração de instruções no encerramento. Uma confirmação positiva do loader lock não resolve ownership do patch, corridas com dispatch, thread-affinity de engine ou plugins terceiros.

## Pareceres anteriores à captura física

Os pareceres abaixo descrevem a preparação baseada nos logs de `20261012-223706`; não interpretam a captura posterior de `20261013-002726`. A observação e a revisão do relatório novo estão registradas ao final.

- **Architect:** NO-GO para integração agora; candidato melhor é observar primeiro a entrada síncrona UAL, sem worker/timer/APC. O plugin-sdk é circular até o patch estar autorizado e seguro.
- **Windows native:** os logs não identificam LoaderLock; propõe consulta por símbolos do PEB e do objeto, com parada imediata/inconclusiva se o PDB não expuser tipo/endereço. Sem offset fixo.
- **Reviewer read-only:** confirma que logs e probe sintético não demonstram lock livre; rejeita callback físico. Aprova somente investigação passiva específica com critério inconclusivo explícito.

## Validação da automação específica — 2026-10-13

`scripts/tests/Test-LoaderLockEvidence.ps1` passou com **8/8 cenários CDB Win32** e **6/6 rejeições do parser**. A suíte compila somente fixtures descartáveis, reutilizando o host `ProbeTarget.cpp` existente. Três capturas na função do host observaram lock livre; a captura de `LockedEntry` chamada pelo `DllMain` da DLL descartável observou lock adquirido e owner igual à TID. Tipo ausente, expressão inválida, guard sintaticamente inválido e exportação ausente resultaram inconclusivos. O guard inválido tomou o cleanup padrão, limpou o breakpoint e não executou a captura. Todos os oito processos permaneceram vivos após detach e concluíram normalmente com código nativo de saída 0, conferido por `GetExitCodeProcess`.

Logs, transcripts e `result.json` desta execução estão em `build/runtime-evidence-loaderlock-tests/2ec9a4fc166f4bb38e26dd817c8c054d/`. Nos casos normais a suíte rejeita `Syntax error`, implicit wait, falha de command file e erro 123; nos testes negativos só permite o erro deliberado. O parser rejeitou Void, PDB ausente, detach ausente, owner/recursão contraditórios, leitura inválida e implicit wait adicionado ao transcript.

Também passaram a auditoria/regressão de sete scripts antigos (`Test-RuntimeEvidenceScripts.ps1`), oito testes do parser da ajuda CDB (`Test-CdbHelpParser.ps1`) e o probe de detach antigo: reproduziu `qd` entregando `STATUS_SINGLE_STEP`, enquanto 3/3 fluxos `.detach /h` concluíram normalmente, além dos caminhos de cancelamento e rejeição. Logs do último probe: `build/runtime-evidence-detach-tests/1c5cd9791e8e4c839f4082d0643eefc1/`. `Invoke-RuntimeEvidence.ps1 -LoaderLockOnly -CheckOnly` passou com CDB x86 `10.0.28000.2957` do Windows SDK, sem iniciar/anexar ao GTA.

A geração foi corrigida para CRLF e retomada como última ação do command file, eliminando erros de sintaxe por fragments de linha e implicit wait encontrados durante a preparação. A revisão final read-only não identificou bloqueadores para a automação passiva. Não foram necessários build/CTest do projeto nem recompilação de ASI: as alterações se limitam a scripts, fixtures de teste e documentação.

Na preparação acima o agente não executou o GTA nem instalou/alterou ASI. A captura física posterior foi realizada manualmente pelo usuário e está registrada a seguir.

## Reprocessamento offline da captura física — 2026-10-13

A captura `build/runtime-evidence/20261013-002726/01-loader-lock.txt` contém `LL_CONTEXT pid=` na linha 183, seguido por mensagens SYMSRV/DBGHELP de carregamento de símbolos do DINPUT8. Os valores `4c80 tid=5b04 eip=715445f0 entry=715445f0 peb=00291000` só aparecem na linha 206. A regex anterior exigia tudo na mesma linha: encontrou zero contextos, embora `LL_CAPTURE_DONE` e `Detached` estivessem presentes. O motivo genérico “Captura incompleta ou detach não confirmado” era incorreto para esse caso.

O parser versão 2 reconstrói exclusivamente o bloco dessa `.printf` até o próximo prompt CDB; remove diagnósticos reconhecidos e continuações identificadas, limitados a 64 linhas/8 KiB. Não extrai números de diagnósticos, stacks ou comandos seguintes para completar campos. Exige sequência única e completa de cinco campos, confirmando PID/TID com a thread atual (`~`) e EIP com os registradores (`r`). Ruído desconhecido, campos duplicados, continuação ambígua ou limites excedidos ficam inconclusivos.

`ReasonCode` distingue captura incompleta (`CAPTURE_INCOMPLETE`), detach não confirmado (`DETACH_UNCONFIRMED`), falha de contexto (`CONTEXT_EXTRACTION_FAILED`), contradições de contexto, símbolos não confirmados, campo/tipo inválido, endereço do campo, ponteiro do objeto e dados da critical section. Essas últimas três verificações são independentes; valores ausentes/repetidos/contraditórios continuam fail-closed. Nenhum offset foi presumido.

Resultado observado no log:

| Evidência | Valor |
|---|---|
| PID / TID | `0x4C80` / `0x5B04`, confirmados pela thread atual |
| EIP / entrada | `0x715445F0`, confirmados pelos registradores e símbolo da exportação |
| PEB | `0x00291000` |
| Endereço do campo | `0x002910A0`, conferido por `&campo` e PEB + offset publicado por `dt` naquele PDB |
| Objeto | `0x779D6354`, igual no campo tipado e no dump do objeto |
| Tipo | `_RTL_CRITICAL_SECTION*`, fornecido pelo PDB, sem cast do objeto |
| Owner / RecursionCount / LockCount | nulo / `0` / `-1` |
| PDB ntdll x86 | GUID `7C1F7D48-576A-AE47-EB48-E43D5FB752AD`, age `1`, carregado com símbolos exatos |
| Encerramento | `LL_CAPTURE_DONE`, `.detach /h`, `Detached` |

Classificação: **`OBSERVADO_LIVRE` / `FREE_SNAPSHOT`**. Significa somente lock livre naquele instante, naquela entrada e configuração. Não demonstra thread-affinity de engine, residência de callbacks, propriedade de patch nem instalação segura; não autoriza integração nativa.

Relatório final: [loader-lock-result.reprocessed-20261013-035044629-35b54951.json](../build/runtime-evidence/20261013-002726/loader-lock-result.reprocessed-20261013-035044629-35b54951.json). SHA-256 do log original: `A4516AA63477D066736C69331F63419B6100660A4A3E56DA5CA5DAA83E171A71`; do JSON original preservado: `13A5B980ACC89B5DA4C6F8895E42C57A90DF708617313BE5671F432F3EB59A93`. Esses valores e o hash do parser usado constam do campo `Audit` e foram reconferidos pela revisão read-only.

O launcher permite repetir somente a análise dos arquivos, retornando antes de localizar CDB, consultar processos ou verificar a instalação do GTA:

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\scripts\Invoke-RuntimeEvidence.ps1' -ReprocessDirectory 'D:\Programacao\Workspace\Applications\grandmp\build\runtime-evidence\20261013-002726'
```

Esse modo é exclusivo: não pode ser combinado com `-LoaderLockOnly` ou `-CheckOnly`. Aceita subdiretórios de `build/runtime-evidence`, rejeita reparse points e gera `loader-lock-result.reprocessed-<UTC>-<id>.json` com `CreateNew`, sem sobrescrever o log ou `loader-lock-result.json`. O campo `Audit` registra UTC, caminhos e SHA-256 do log, JSON original e código do parser usado.

`scripts/tests/Test-LoaderLockParser.ps1`: **49/49 PASS** offline neste workspace, com log real integral, fixture de trechos reais versionado, formato antigo numa linha, múltiplas intercalações, lock ocupado, casos negativos de todas as validações, limites de contexto, oito logs antigos do laboratório reanalisados sem CDB, exclusividade do modo, confinamento do caminho e preservação dos originais. Nenhum CDB/GTA, executável de teste ou build de ASI foi iniciado durante essa correção. A revisão final read-only confirmou a lógica fail-closed e os hashes do novo relatório; os pareceres históricos acima não alteram a classificação deste snapshot. A pendência de medição está encerrada, sem autorização para avançar a integração com o motor.
