# GrandMP — Hoodlum Runtime Evidence (Alpha 0.3.4, Slice 3)

**Medição específica posterior de LoaderLock:** use somente `scripts/Invoke-RuntimeEvidence.ps1 -LoaderLockOnly` depois da autorização física. Esse modo captura `InitializeASI` e o objeto do lock por símbolos; encerra antes de qualquer captura de call-site. Seu procedimento, resultados offline e interpretação estão em [GTA Bootstrap Readiness](gta-bootstrap-readiness.md#captura-automatizada-específica-de-loaderlock). O fluxo original sem esse parâmetro continua suspenso conforme abaixo.

Este roteiro documenta o launcher semiautomatizado para `D:\Jogos\GrandMP-Smoke`. Ele não instala hooks, PIN, callbacks ou timers nem altera `GrandMP.asi` ou o loader. O debugger suspende brevemente o processo; o launcher usa eventos de depuração e breakpoints de execução de hardware, sem inserir instruções no GTA. **O fluxo automatizado atual está bloqueado para novas execuções:** os scripts `capture-menu.wds` e `capture-gameplay.wds` executam `t`, que nas capturas físicas causou implicit wait/HRESULT `0x80004005`. Não execute o launcher para repetir essas etapas até que uma substituição seja validada com CDB e processo descartável. As instruções manuais antigas abaixo são apenas referência histórica e também não devem ser executadas neste slice porque usam `bu` (breakpoint de software). Cada sessão é um experimento, não smoke aprovado.

## Preflight Slice 3 — leitura local em 2026-10-12

Sem iniciar o GTA, foram conferidos novamente os arquivos da instalação isolada:

- `gta_sa.exe`: 14.383.616 bytes, SHA-256 `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`.
- `scripts\GrandMP.asi`: 593.920 bytes, SHA-256 `1EB0E9E22591402FA6002363DD0FC22B873B27F8224EA11C1FA3B356EEC7D2FA`.
- `dinput8.dll`: 1.699.840 bytes, SHA-256 `29FEE0AA46414864AD1F65B2689B470BFB2CF2F09A94E2AFD271E68F2E16743B`.
- `global.ini` e `scripts\global.ini` contêm `DontLoadFromDllMain=1`; isso é configuração observada, não prova de loader-lock livre.
- O único `.asi` encontrado nas pastas raiz, `scripts`, `plugins` e `update` foi `scripts\GrandMP.asi`.
- O ASI instalado tem hash igual ao artefato em `build\alpha027-win32\client\windows\Debug\GrandMP.asi`; há um `GrandMP.pdb` ao lado. O WinDbg ainda deve confirmar correspondência de símbolos com `lmv m GrandMP` antes de confiar em nomes/linhas.

O Windowed Mode de terceiros fica desativado durante todo o procedimento, conforme solicitado. Nenhuma configuração foi alterada. Esta seção registra o preflight anterior à sessão física; as observações obtidas depois estão consolidadas em [Native Callback Readiness](native-callback-readiness.md), com os logs brutos em `build/runtime-evidence/20261012-223706`.
## Baseline e pré-requisitos — registro histórico da Alpha 0.2.10

Na preparação original deste roteiro, durante a Alpha 0.2.10, havia zero commits e **64 arquivos não rastreados**. A proposta de primeiro commit, ainda não executada, é `chore(repo): establish GrandMP foundation through alpha 0.2.10`, incluindo esses 64 arquivos; este relatório fica fora dessa baseline. `.gitignore` cobre `/build/`, caches, binários (`.exe`, `.dll`, `.asi`, `.pdb`), logs, `.env` e arquivos locais. Os checkouts de pesquisa e builds estão sob `/build/` e `git check-ignore` confirmou sua exclusão. A revisão de nomes, tamanhos e padrões de segredo não encontrou executáveis, assets proprietários do GTA ou credenciais no conjunto elegível. Fontes terceiras permanecem em checkouts ignorados; suas licenças e limites estão em [experimental-callback-decision.md](experimental-callback-decision.md).

Leituras locais, sem executar o jogo, confirmaram:

| Arquivo | Evidência |
| --- | --- |
| `D:\Jogos\GrandMP-Smoke\gta_sa.exe` | SHA-256 `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26` (Hoodlum aprovado). |
| `D:\Jogos\GrandMP-Smoke\scripts\GrandMP.asi` | SHA-256 `1EB0E9E22591402FA6002363DD0FC22B873B27F8224EA11C1FA3B356EEC7D2FA`; coincide com `build\alpha027-win32\client\windows\Debug\GrandMP.asi`. |
| PDB candidato | `D:\Programacao\Workspace\Applications\grandmp\build\alpha027-win32\client\windows\Debug\GrandMP.pdb`, no mesmo diretório da build cujo ASI tem hash idêntico ao instalado. A coincidência do ASI não comprova por si só GUID/age do PDB; confirmar em `lmv m GrandMP` que o depurador carregou símbolos correspondentes. |
| Loader instalado | `dinput8.dll` Win32 7.7.0, SHA-256 `29FEE0AA46414864AD1F65B2689B470BFB2CF2F09A94E2AFD271E68F2E16743B`; `global.ini` e `scripts\global.ini` declaram `DontLoadFromDllMain=1`. Essas leituras não provam o estado do loader lock durante `InitializeASI`. |
| ASIs nas pastas reconhecidas | Nesta inspeção, somente `scripts\GrandMP.asi` foi encontrado na raiz, em `scripts`, `plugins` ou `update`; conferir novamente antes do teste. |

**Na inspeção anterior, WinDbg/CDB x86 não foi encontrado no PATH nem nos caminhos usuais do Windows SDK.** A verificação atual (launcher Slice 3, abaixo) localizou o alias WinDbgX moderno, mas continua sem CDB x86. O usuário deve instalar manualmente o [WinDbg oficial da Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/). O WinDbg moderno permite selecionar **Target bitness: 32 bit**; WinDbg clássico x86 também serve. Nenhum instalador foi executado por este trabalho. É necessário acesso aos símbolos públicos do Windows ou um cache já preenchido. O PDB GrandMP acima é necessário para nomes e linhas internas confiáveis; GTA e loader podem aparecer apenas como módulo+deslocamento. [Guia oficial para iniciar um executável e escolher diretório/bitness](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/windbg-user-mode-preview).

Mantenha o Windowed Mode de terceiros desativado durante toda a investigação. Antes de abrir WinDbg, confirme novamente os hashes dos arquivos usados, deixe somente plugins conhecidos da instalação isolada ativos e crie manualmente `D:\Programacao\Workspace\Applications\grandmp\build\runtime-evidence\` para logs. Não alterar o loader ou o ASI. Encerrar outras instâncias do GTA para que o PID da sessão seja inequívoco.

## Sessão 1 — carregamento e `InitializeASI`

1. Em WinDbg, escolher **File > Start debugging > Launch executable (advanced)**. Executable: `D:\Jogos\GrandMP-Smoke\gta_sa.exe`; Start directory: `D:\Jogos\GrandMP-Smoke`; Target bitness: **32 bit**; sem argumentos adicionais. Aguardar a parada inicial do depurador antes de continuar.
2. Na janela **Command**, executar linha a linha:

```text
.symfix
.sympath+ D:\Programacao\Workspace\Applications\grandmp\build\alpha027-win32\client\windows\Debug
.reload /f
.logopen /t D:\Programacao\Workspace\Applications\grandmp\build\runtime-evidence\windbg-runtime.txt
sxe ld:GrandMP
bu /1 GrandMP!InitializeASI
bl
g
```

`sxe ld:GrandMP` interrompe no evento de carga do módulo; `bu /1` já deixa armado um breakpoint simbólico de uso único para a exportação, caso a parada de carga não permita armá-lo a tempo. A [Microsoft documenta o filtro de carga](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/sx--sxd--sxe--sxi--sxn--sxr--sx---set-exceptions-) e [breakpoints de uso único](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/bp--bu--bm--set-breakpoint-). No evento de carga, **não** interpretar a stack como a de `InitializeASI`; registrar e continuar:

```text
lmv m GrandMP
lmv m dinput8
x GrandMP!InitializeASI
bl
g
```

Se `lmv m GrandMP` não mostrar o PDB carregado com correspondência, preservar o log e interromper a conclusão baseada em nomes/linhas internas; não forçar um PDB incompatível.

3. Na parada em `InitializeASI`, confirmar que `eip` coincide com o endereço da exportação e registrar:

```text
~
r
kv 100
kb 100
dds @esp L80
lmv m GrandMP
lmv m dinput8
lmv m gta_sa
lmv m ntdll
lmv m kernel32
lmv m kernelbase
!ntsdexts.locks
```

`~` mostra os TIDs; o índice do WinDbg não é o TID do Windows. `kv`/`kb` dependem da qualidade do unwind e dos símbolos; na primeira instrução da exportação a pilha pode estar incompleta. `dds @esp L80` preserva os valores brutos para comparação com os módulos. Rotular frames de GTA, loader ou sistema sem símbolos confiáveis como **endereço de módulo+deslocamento**, nunca como função comprovada. Guardar a saída de `lmv`, que informa caminho, versão e estado dos símbolos. [Comandos de stack](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/k--kb--kc--kd--kp--kp--kv--display-stack-backtrace-), [thread](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/---thread-status-) e [símbolos](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/symbol-path).

`!ntsdexts.locks` é um **diagnóstico opcional**. Se ele identificar inequivocamente a seção crítica do loader, anotar endereço, `OwningThread`, `RecursionCount` e se o TID atual é o dono; confirmar o endereço com `!critsec <endereco>` se disponível. A [documentação Microsoft de seções críticas em user mode](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/displaying-a-critical-section) explica esses campos. Se a extensão, o símbolo ou a associação com o loader lock não forem confiáveis, marcar **estado do lock inconclusivo**. A ausência de frames `Ldr` na stack **não** demonstra que o lock esteja livre.

4. Após registrar, usar `g` para chegar ao menu e continuar com a segunda sessão na mesma execução.

## Sessão 2 — call-site `gameProcessEvent` no Hoodlum (Slice 3)

Esta captura substitui o breakpoint anterior na entrada `0x53BEE0`: primeiro observamos o CALL associado ao evento em `0x53E981`, que é o ponto deste experimento. O breakpoint usa registrador do processador (`ba`), é temporário e de uso único (`/1`); não troca bytes da instrução ([sintaxe e efeito documentados pela Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/ba--break-on-access-)). Não registrar callback, instalar hook ou editar registradores/memória.

Mantenha o Windowed Mode de terceiros desativado. Depois de capturar `InitializeASI` e continuar até o menu, pressione **Ctrl+Break** em WinDbg. Confira a base real com `lmv`. Se a base for `0x00400000`, copie o bloco abaixo. Se for diferente, substitua a base nas duas expressões pela base mostrada por `lmv`:

```text
lmv m gta_sa
r @$t0 = 0x00400000+0x13e981
r @$t1 = 0x00400000+0x13bee0
? @$t0
? @$t1
```

`@$t0` passa a guardar o call-site; `@$t1`, o alvo candidato. Pare sem armar breakpoint se o hash/arquitetura não for Hoodlum aprovado ou se a base não puder ser confirmada.

No menu, confira bytes e instruções antes de armar o breakpoint. Esperado: `E8 5A D5 FF FF` no call-site e `83 EC 0C 53 56 57` no alvo:

```text
db @$t0 L5
u @$t0 L1
db @$t1 L6
u @$t1 L6
```

Qualquer divergência torna a observação inconclusiva. Se conferirem, arme um breakpoint de execução de uso único no **CALL**, não na entrada da função:

```text
ba e 1 /1 @$t0
bl
g
```

Na parada, anote `menu`, confirme `eip` igual a `@$t0` e capture o contexto antes do CALL:

```text
~.
~
r
kv 80
dd @esp L4
dds @esp L40
lmv m gta_sa
lmv m GrandMP
lmv m dinput8
db @$t0 L5
? @$t0+5
```

Depois execute exatamente um passo para atravessar o CALL:

```text
t
r
dd @esp L4
kv 80
dds @esp L40
```

Nas capturas físicas de 2026-10-12, `t` mostrou `eip=0x0053BEE0` e `esp` quatro bytes abaixo do valor pré-CALL. Isso é consistente com a entrada no destino; `0x0053E986` como retorno empilhado é inferido do CALL e da diferença de ESP, pois o dump pós-passo solicitado não foi executado. O CDB reportou `Command file caused an implicit wait` / `HRESULT 0x80004005`; os comandos posteriores do arquivo não são evidência de execução completa. Por isso, esse passo manual histórico não valida a automação atual nem deve ser repetido dentro de um command file sem teste CDB descartável. Consulte [Native Callback Readiness](native-callback-readiness.md) para os limites e o estado da evidência.

Para a partida, inicie-a manualmente, aguarde CJ aparecer e ficar estável, e pressione **Ctrl+Break**. Confirme que o processo continua com a base e bytes esperados. Arme uma nova ocorrência de uso único:

```text
lmv m gta_sa
db @$t0 L5
db @$t1 L6
ba e 1 /1 @$t0
bl
g
```

Na parada, repita a captura pré-CALL acima, identifique o estágio como `partida`, execute um único `t` e capture novamente `r`, `dd @esp L4`, `kv 80` e `dds @esp L40`. Anote se CJ estava parado. Não leia estruturas, não chame funções do GTA e não use breakpoint persistente.

Se o CALL não disparar em cerca de cinco segundos no estágio observado, pressione **Ctrl+Break**, rode `bl` e registre **nenhuma ocorrência observada**. Não conclua que o call-site não seja executado nessa fase.

Uma parada em `@$t0` e o passo para `@$t1` demonstram diretamente que esse call-site executou e transferiu controle ao alvo naquela captura. Repetir no menu e durante partida apoia a hipótese de que o caminho ocorre nos dois estágios. Isso não prova que seja o ciclo completo de atualização, thread-affinity segura para APIs do motor, instalação sem corrida, propriedade do patch, compatibilidade de outros builds nem segurança de inscrição/remoção de callbacks. A prioridade `AFTER` do plugin-sdk vem do wrapper-fonte; não é observada apenas com este breakpoint.

`lmv` registra módulos, caminhos e estado dos símbolos. Frames sem símbolos correspondentes devem ser relatados como módulo+deslocamento; não atribua nome de função sem símbolo confiável.
## Encerramento, recuperação e evidência a devolver

Ao terminar, enquanto WinDbg estiver parado, executar `bl`, `bc *`, `.logclose` e `g`; sair normalmente pelo menu do GTA. Se precisar retirar o depurador com o jogo ainda aberto, usar `.detach` depois de limpar breakpoints; [`.detach` é documentado pela Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/-detach--detach-from-process-). Se o jogo parecer congelado, primeiro verificar se WinDbg o deixou **parado num breakpoint**: pressionar Ctrl+Break se necessário, listar/limpar breakpoints e continuar ou destacar. Se o processo não responder mesmo destacado, encerrar `gta_sa.exe` manualmente pelo Gerenciador de Tarefas e guardar o log; um progresso não salvo pode ser perdido. Nunca tentar liberar hook, pois nenhum foi instalado.

Retornar o arquivo de log WinDbg (ou trechos integrais relevantes), hashes do `gta_sa.exe`, `GrandMP.asi` e `dinput8.dll`, versão do Windows/WinDbg, estado dos símbolos em `lmv`, PID/TID das duas paradas, `r`, `kv`/`kb`, `dds`, saída de `!ntsdexts.locks`/`!critsec` se disponível, base real do `gta_sa` e bytes lidos antes do breakpoint, estágio do jogo e resultado do encerramento. O log pode conter caminhos locais, endereços e nomes de módulos; revisar antes de compartilhar. **Não enviar executáveis, PDBs, assets GTA ou dumps completos.**

Conclusão possível: lock identificado com dono igual ao TID atual na entrada indica execução sob lock; lock identificado livre naquele instante oferece evidência de ausência naquele instante, sujeita à validade dos símbolos/extensão. Sem identificação confiável, o gate de loader lock continua inconclusivo. Uma chamada de `CGame::Process` em menu e outra em partida, com bytes/retornos coerentes, confirma que a entrada observada é executada nessas fases; não prova ABI de todos os chamadores, disponibilidade do jogador, segurança de instalação de hook, residência de callback ou estabilidade futura. Nenhuma aprovação de hook decorre automaticamente destas observações.

## Launcher semiautomatizado — Slice 3

O launcher `scripts/Invoke-RuntimeEvidence.ps1` foi criado para automatizar as três capturas sem digitar comandos no debugger. Ele usa `cdb.exe` x86, incluído em **Debugging Tools for Windows**, e arquivos `.wds` com a mesma linguagem de comandos do WinDbg. **No estado atual, use somente `-CheckOnly`: as capturas de call-site estão suspensas porque `t` falha dentro do command file. Não execute o launcher sem `-CheckOnly` até uma alternativa ser testada em processo descartável.**

Para a verificação permitida, abra PowerShell normal (sem elevação) e execute somente `-CheckOnly`:

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\scripts\Invoke-RuntimeEvidence.ps1' -CheckOnly
```

O launcher:

1. Localiza `cdb.exe` x86 no Windows SDK/PATH, confirma arquitetura PE x86 e consulta `cdb.exe -?` para verificar os parâmetros efetivamente usados: `-c`, `-cf` e `-p PID`. O encerramento é feito por `.detach /h` seguido de `q`, sem depender do `-pd`.
2. Confere SHA-256 do GTA Hoodlum, GrandMP.asi e Ultimate ASI Loader, PE32, ImageBase preferida, ausência de outro GTA em execução e ausência de ASIs desconhecidos nas pastas conhecidas. Ele não pede privilégios e não instala ferramentas.
3. **Suspenso:** no fluxo histórico, após `SIM`, iniciava o GTA sob o CDB e capturava `InitializeASI`. Nesta etapa não execute essa modalidade; somente `-CheckOnly` está liberado. A captura física anterior de `InitializeASI` está nos logs referenciados neste documento.
4. **Suspenso:** o código atual valida bytes do CALL/prólogo e arma breakpoint one-shot, mas o script `t` seguinte provoca implicit wait/HRESULT `0x80004005`. Assim, as capturas físicas de menu/partida não completaram o command file nem comprovaram o caminho de detach dessa etapa. Não repita até validar uma alternativa com CDB e processo descartável.
5. A captura física rotulada como menu pode ter ocorrido na transição para gameplay; confirme visualmente a fase em uma futura investigação, sem inferi-la do rótulo do launcher. A sessão disponível não comprova menu e partida como estados distintos.

O caminho dos artefatos é `build/runtime-evidence/<data-hora>/`, com `01-initialize-asi.txt`, `02-menu-callsite.txt`, `03-gameplay-callsite.txt` e os `.wds` efetivamente usados. Para verificar somente a disponibilidade/CLI do debugger, sem iniciar ou anexar ao GTA, use:

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\scripts\Invoke-RuntimeEvidence.ps1' -CheckOnly
```

Neste workspace, em 2026-10-12, `cdb.exe` x86 foi localizado no Windows SDK e `-CheckOnly` confirmou a CLI local. O modo x86 é validado pelo cabeçalho PE do próprio executável, não inferido do nome do diretório. A CLI consultada segue a [documentação oficial do CDB/WinDbg](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/cdb-command-line-options). As condições de bytes dos scripts de menu/partida ficam em uma única linha `.if`: `$<` converte quebras de linha em separadores de comandos, então um bloco `.if` multilinha pode falhar sintaticamente sem interromper o restante do arquivo. O breakpoint e `g` existem somente dentro do ramo aprovado; depois do `.if` há apenas fechamento, detach e saída. O teste com CDB x86 cobre bytes válidos/adulterados, leitura inválida e sintaxe defeituosa. Uma notificação `epr` faz o CDB sair se o GTA encerrar antes de atingir o call-site; enquanto o jogo permanecer aberto e o call-site validado não for atingido, o CDB aguarda. Encerre o GTA normalmente para liberar essa espera; não mate o CDB à força.

O encerramento após um hardware breakpoint usa [`.detach /h`](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/-detach--detach-from-process-), que continua eventos pendentes marcando-os como tratados; o CDB só recebe `q` depois do detach. Não se usa `qd` enquanto o evento `STATUS_SINGLE_STEP` está pendente. Os breakpoints `ba e 1 /1` são breakpoints de execução de hardware one-shot, conforme a [referência `ba`](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/ba--break-on-access-); nenhum `bp` de produção, patch, hook, escrita no processo ou chamada de API do GTA é usado.

### Intervenção e recuperação

As confirmações Enter do launcher pedem ao operador que informe quando o menu/partida parecem prontos; elas não validam os estados do jogo. Nas capturas físicas atuais, as etapas InitializeASI tiveram detach registrado, mas as etapas de call-site falharam no `t`; não generalize o detach já testado em outros probes para essas capturas. O teste Win32 descartável em `scripts/tests/Test-CdbHardwareBreakpointDetach.ps1` validou o fluxo de detach para o cenário coberto por aquele teste, não corrige o erro de implicit wait. Se o debugger parar inesperadamente, preserve o log e os códigos de exceção; não use `qd` cegamente nem marque uma exceção desconhecida como tratada. Não encerre o GTA pelo gerenciador enquanto ainda estiver anexado. Breakpoints de hardware não substituem bytes de instrução; ainda assim, anexar/debugar suspende o processo brevemente e pode alterar o timing.

As sessões manuais abaixo são registro histórico. Elas contêm `bu`, que instala breakpoint de software e substitui temporariamente um byte no processo; **não as use neste slice**, cujo launcher evita essa alteração. Não execute simultaneamente o launcher e o roteiro histórico.
