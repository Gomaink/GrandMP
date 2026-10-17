# Alpha 0.3.6 / Slice 5 — Physical ASI Readiness

## Decisão

**GO para preparação do mesmo ASI experimental no host cooperativo existente. NO-GO para smoke de callback no GTA.** O artefato agora contém READY v1, preparação explícita, PIN, shutdown e o mesmo wrapper contador testado na DLL da Slice 4. No GTA, `InitializeASI` continua somente solicitando inicialização: estado REQUESTED, sem PIN, READY ou instalação.

O impedimento específico continua sendo **quem chamará a preparação diferida no GTA, sob qual contrato verificável**. A exportação `GrandMPNativeCounterPrepareDeferred` admite exclusivamente o host contador; `gta_sa.exe` é recusado antes de PIN. Não foi inventado worker, timer, hook prévio ou chamada pelo debugger para preencher essa lacuna. O sucesso do host não autoriza mudar essa admissão para o GTA.

Referências reutilizadas: [ROADMAP](ROADMAP.md), [transação externa Slice 4](external-patch-transaction.md), [evidência e decisão Slice 3](native-game-thread-integration.md), [snapshot LoaderLock](gta-bootstrap-readiness.md). As descrições históricas de que o experimental não exportava READY/wrapper descrevem as builds anteriores; esta slice acrescenta essas exportações, mantendo a execução GTA inativa.

## Código e estado efetivo

| Arquivos | Entrega |
|---|---|
| `client/windows/tools/native_counter_experimental.cpp` e `.def` | READY/wrapper compartilhados com laboratório, PrepareDeferred sem argumentos, Shutdown; DllMain mínimo |
| `client/windows/tools/native_counter_ready.hpp` | Interface v1/80 bytes preservada; aliases UNINITIALIZED/SHUTDOWN e INITIALIZING=5 |
| `client/windows/tools/native_counter_host.cpp` | Cenários do ASI real no mesmo host, incluindo corrida preparação/shutdown |
| `client/windows/tools/native_counter_preflight.hpp/.cpp` | Leitura de arquivo, SHA-256 CNG e validação limitada de evidências físicas; escrita GTA desabilitada |
| `tests/native_counter_preflight_tests.cpp` | Casos positivos/negativos de evidências, hashing real e exportações do ASI produzido |
| `client/windows/CMakeLists.txt`, `tests/CMakeLists.txt` | Alvos/testes e `/JMC-`, `/INCREMENTAL:NO` para experimental auditável |
| `docs/physical-asi-readiness.md` | Handoff, gates, política proposta de contenção e seleção futura dos commits |

Nenhuma mudança no ASI homologado (`asi_entry.cpp`/`GrandMP.def`), servidor, UDP, Android ou adapter foi necessária. Alterações anteriores foram preservadas.

### Interface e estados

Sete exports do experimental: `InitializeASI`, `GrandMPNativeCounterInstallationStatus`, `GrandMPNativeCounterRead`, `GrandMPNativeCounterReady` (DATA), `GrandMPNativeCounterWrapperHost`, `GrandMPNativeCounterPrepareDeferred`, `GrandMPNativeCounterShutdown`. O sufixo WrapperHost foi preservado para reutilizar o contrato v1; ele não significa wrapper homologado no motor.

| Estado | Código | Transição / garantia |
|---|---|---|
| UNINITIALIZED | 0 | Módulo carregado, sem solicitação/PIN |
| REQUESTED | 1 | InitializeASI faz solicitação atômica; não executa preparação |
| INITIALIZING | 5 | Preparação explícita admitida, ainda sem READY |
| READY | 2 | Todos os campos completos; PIN confirmado; publicação final release |
| FAILED | 4 | Falha terminal; nenhuma reativação por InitializeASI repetido |
| SHUTDOWN | 3 | Desativação lógica terminal e idempotente; sem unload/restauração |

Os códigos 0–4 e o layout v1 permanecem compatíveis com a Slice 4. Estado 5 nunca é admissível ao writer/preflight. O descriptor é armazenamento estático do módulo; os campos de endereço não são publicados como READY antes de estarem completos.

### Contexto admitido e PIN

O contexto demonstrado é uma chamada explícita do host **após LoadLibrary retornar**, com seu ciclo de preparação controlado. PrepareDeferred resolve o módulo principal, exige nome `grandmp_native_counter_host.exe`, PE32/i386, exports conhecidos CALL/original, regiões RX da imagem e bytes compatíveis. Não aceita endereços ou alegações de contexto fornecidos por argumento.

O host é cooperativo. Nome/exports não são autenticação contra um executável renomeado/adversário e não provam ausência de loader lock para um chamador arbitrário. O contrato é confirmado pelo código e testes do host, não por uma função pública universal de detecção de LoaderLock.

O caminho compartilhado valida FROM_ADDRESS contra o próprio HMODULE e adquire PIN efetivo. PIN é irreversível até exit. `residencyConfirmed=1` é registrado depois da aquisição mesmo se shutdown vencer antes do bind; nesse caso **não há READY**, mas o diagnóstico não oculta que o módulo ficou residente. Falha anterior a PIN deixa residência zero. READY é sempre a última publicação após bind/invariantes.

Preparação e shutdown são serializados pelo mesmo mutex. Nenhum mutex é tomado pelo helper contador. Shutdown pode vencer entre PIN e bind; o bind recusa e preserva SHUTDOWN, sem regredi-lo para FAILED. Chamadas já admitidas podem terminar um incremento depois de Shutdown retornar: **shutdown lógico não é quiescência**. A residência protege a duração do código/estado; nenhum resultado autoriza FreeLibrary como teardown ou hot reload. DllMain apenas registra HMODULE; não restaura instruções nem espera callbacks.

### Por que não preparar no InitializeASI do GTA

O UAL 7.7.0 chama a exportação sincronicamente no trigger admitido. Na evidência física anterior, a entrada foi observada durante delay-load de DirectInput8Create, com quatro threads, e um snapshot LoaderLock livre. Isso não constitui um contrato para toda chamada futura/configuração. `AsiBootstrapController::beginDeferred()` exige um ponto seguro estabelecido pelo chamador; o UAL não chama PrepareDeferred e nenhum chamador GTA adicional foi comprovado.

Esta slice usa a evidência encerrada para delimitar o bloqueio; não repete depuração ou leitura do GTA. Os logs `20261012-223706` e `20261013-002726` continuam sendo snapshots históricos. Não existe handshake que invoque código do alvo parado; preparação deve acontecer enquanto o alvo executa normalmente em contexto admitido, antes de a transação externa conservar seu evento.

## Preflight específico, somente leitura

`readNativeCounterFile()` usa CreateFileW/GENERIC_READ, compartilhamento somente leitura, tamanho limitado a 32 MiB, leitura integral e SHA-256 por BCrypt. Retorna bytes, inspeção PE, ImageBase e SizeOfImage. O handle é fechado ao retornar: a evidência vale para aquele instante; um futuro collector/writer deve conservar identidade/handles ou reconfirmar o arquivo, não presumir que ele ficou imutável depois.

`inspectNativeCounterPhysicalPreflight()` reutiliza `recognizeGameVersion()` e o codec rel32. Verifica:

- SHA-256/tamanho Hoodlum homologados, PE32/i386, ImageBase e base carregada `0x00400000`.
- Imagem experimental admitida, digest do arquivo carregado igual ao esperado, exports DATA/PrepareDeferred/wrapper, seções e bounds; rejeita forwarders, imagem truncada e versão incompatível.
- Dois snapshots READY iguais; estado, versão, tamanho, PID, base/tamanho do módulo, endereços dos exports, contador alinhado dentro de seção de dados, PIN/capacidade/shutdown/installed/reserved.
- CALL exato em `0x53E981`, bytes `E8 5A D5 FF FF`, destino `0x53BEE0`, incluindo a cópia publicada no descriptor.

O preflight **valida evidências fornecidas**; ainda não implementa collector remoto completo, associação do arquivo ao módulo por eventos de carga, controle de event ownership/contextos ou writer GTA. `residencyConfirmed` isolado não prova PIN: sua confiabilidade depende da imagem admitida e do caminho auditado. Resultado positivo chama-se `evidence_consistent_read_only`; `nativeCounterGtaWriteEnabled=false` em todos os casos.

Um futuro writer deve recolher essas evidências independentemente e reconfirmar CALL/contextos sob o mesmo evento pendente, imediatamente antes da escrita. A parada exclui threads do alvo durante a transação, não writers externos, alterações posteriores a ContinueDebugEvent ou plugins hostis. Não encadear patches desconhecidos. O ASI preparado no host publica endereços do **host**, portanto não passa a identidade específica Hoodlum só por estar READY.

## Wrapper e ABI

Revisão fixada: plugin-sdk `15f15b60bbf74c106e1b496ff92c98764abf4605`, `shared/Events.h`, `meta.CGame.h`; evidência física Hoodlum do CALL/destino permanece a da Slice 3. As referências de Process em meta.CGame.h são rotuladas Compact; não equivalem automaticamente ao arquivo Hoodlum. A assinatura publicada `void __cdecl()` sem argumentos é coerente com o call-site observado, mas não demonstra toda sensibilidade do binário.

O binário experimental Win32 foi auditado por dumpbin 14.51.36260: guard original-nulo retorna preservando flags; caminho preparado faz CALL indireto original exatamente uma vez, PUSHFD/PUSHAD, 528 bytes alinhados a 16, FXSAVE/helper/FXRSTOR, restauração ESP, POPAD/POPFD/RET. O helper aplica gates e `fetch_add` lock-free (LOCK CMPXCHG8B). Não há engine, rede, entidades, I/O, alocação ou mutex no helper.

Limites importantes:

- A CALL extra faz o original observar ESP temporariamente quatro bytes abaixo e um endereço de retorno do wrapper. Restaurar ESP final no laboratório **não prova** que o Hoodlum seja insensível a alinhamento/retorno/stack walking. Esse ponto exige auditoria específica antes de homologar o wrapper real.
- FXSAVE preserva x87/MMX/MXCSR/XMM; requer área alinhada a 16 e CPU FXSR/SSE/SSE2, conferidos antes do bind. Não cobre estado AVX superior. O contrato C++ x86 exige DF limpo; não há promessa para ABI arbitrária.
- O destino é imutável depois do bind e é recusado se apontar ao próprio wrapper. No host, aponta ao export original conhecido. No GTA não é configurado ou chamado. A própria rotina original conserva sua semântica/possível reentrância; o contador não inicia recursão.
- Os testes x64 validam preflight/contratos/regressões, **não** esse wrapper x86.

Contagens da Slice 4: 1 chamada ABI + 8.000 em quatro threads, **após instalação habilitada**, geram `original=count=8001`. Depois de desligar diagnóstico, 100 chamadas produzem `original=8101/count=8001`; mais 10 após FreeLibrary/PIN produzem `8111/8001`. As 110 adicionais foram posteriores ao shutdown, não anteriores à instalação. Nos cenários ASI novos, uma chamada habilitada e duas desligadas resultam `original=3/count=1`.

## Validação executada

Visual Studio 18 2026, MSVC 19.51.36260, SDK 10.0.26100.0, Debug:

| Arquitetura | Build | CTest final |
|---|---|---|
| Win32 | PASS, exit 0 | **158/158**, 23,28 s |
| x64 | PASS, exit 0 | **117/117**, 2,68 s |

Novos testes Win32 do **mesmo ASI produzido**: `asi_ready`, `asi_concurrent` (16 preparações, somente uma aceita), `asi_not_requested`, `asi_partial` (bytes reais do host alterados/controlados antes de PIN), `asi_shutdown`, `asi_shutdown_race` (16 preparações + 16 shutdowns). READY/PIN, flags/stack/x87/SSE e forward original são verificados; referência externa liberada não descarrega o código PINado.

Seis testes de preflight nas duas arquiteturas: arquivo/SHA real (`abc`, digest conhecido), identidade/arquitetura/base, estados/estabilidade/PIN, módulo/forwarders/truncamento, CALL alterado/parcial e identidade/política. O caso positivo de evidências utiliza PE/metadata sintéticos e **não** comprova GTA identificado em execução.

Regressão adicional Win32 `preflight_actual_asi`: lê o ASI real, valida seu PE/exports pelo preflight, carrega-o num executável não admitido, chama InitializeASI e confirma PrepareDeferred recusado, REQUESTED/PIN=0, resultado `not_ready`, unload normal. Corrige a lacuna inicial de testar exports apenas com fixture sintético.

Os testes anteriores, incluindo as 22 transações externas reais no host e os cenários FATAL, permaneceram aprovados. Não foi alterado o writer de laboratório para aceitar GTA ou o experimental .asi.

```powershell
cmake -S . -B build/alpha036-win32 -G "Visual Studio 18 2026" -A Win32
cmake --build build/alpha036-win32 --config Debug --parallel 4
ctest --test-dir build/alpha036-win32 -C Debug --output-on-failure
cmake -S . -B build/alpha036-x64 -G "Visual Studio 18 2026" -A x64
cmake --build build/alpha036-x64 --config Debug --parallel 4
ctest --test-dir build/alpha036-x64 -C Debug --output-on-failure
```

Logs ignorados em `build/alpha036-{win32,x64}/slice5-build.txt`, `slice5-ctest.txt`, `slice5-ctest-detail.log`; auditoria `build/alpha036-win32/slice5-wrapper-disassembly.txt` e `slice5-asi-exports.txt`.

Artefato: `D:\Programacao\Workspace\Applications\grandmp\build\alpha036-win32\client\windows\Debug\GrandMPNativeCounterExperimental.asi`; SHA-256 `463EB60E79FED6E9D6DBD344BD7B33270E2B357AB49FFD1A88942E6EDB022F7B`. O hash corresponde a esta build; não é homologação física ou autorização de instalação.

## Validação manual disponível

Somente laboratório, sem GTA ou cópia de arquivos:

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\build\alpha036-win32\client\windows\Debug\grandmp_native_counter_host.exe' 'D:\Programacao\Workspace\Applications\grandmp\build\alpha036-win32\client\windows\Debug\GrandMPNativeCounterExperimental.asi' asi_ready
```

Esperado: `PASS READY=1 PIN=1 original=3 count=1`, exit 0. O texto READY/PIN descreve o estado alcançado antes do desligamento lógico dentro do teste, não READY no GTA.

## Smoke GTA futuro: roteiro condicionado, ainda inexequível

Não há comando de instalação/patch nesta entrega. Não instalar o experimental para tentar contornar a ausência de caller. Quando os gates abaixo forem satisfeitos **e o usuário autorizar o experimento**, o roteiro mínimo será:

1. Com o jogo fechado, usuário confirma instalação isolada `D:\Jogos\GrandMP-Smoke`, hash Hoodlum, UAL Win32 7.7.0/configuração admitida, Windowed Mode desativado, inventário conhecido sem plugins desconhecidos, ASI/PDB experimental exatos e opt-in. Preservar GrandMP.asi homologado; instalação experimental somente manual e autorizada.
2. Aprovar previamente a política de contenção específica da sessão, inclusive eventual encerramento sem salvar. Sem essa aprovação, nenhum writer GTA poderá iniciar a escrita.
3. Iniciar manualmente sessão supervisionada. Caller admitido executa preparação leve enquanto o alvo está rodando; PIN e READY completos precedem a parada. Não usar `.call`, thread de engine arbitrária ou funções internas para obter READY.
4. Debugger conserva o evento READY pendente. Collector verifica PID/TID, imagens/hashes/configuração, exportações, snapshots, PIN/capacidade, bytes/destino e contextos de todas as threads. Qualquer divergência recusa antes da escrita.
5. Writer GTA especificamente revisto reconfirma bytes e faz a transação completa sob a mesma parada. Só continuar depois de readback/decode, flush, proteção e commit confirmados. Esses componentes de coleta/admissão física ainda não existem nesta slice.
6. Coletar contador/thread sem I/O no callback em menu visualmente confirmado, loading e partida; observar forwarding e encerramento normal. Não acessar entidades. Esse seria dado físico novo; budget/soak/dez ciclos continuam requisitos posteriores do marco.
7. Falha parcial exige rollback confirmado sob o evento pendente. Falha indeterminada não recebe ContinueDebugEvent, detach ou retomada automática. Preservar endereço, bytes, proteções, contexts e erro; aplicar **somente a contenção expressamente autorizada pelo usuário**.
8. Rollback de arquivos apenas depois de exit: retirar/desabilitar experimental opt-in, preservando loader/ASI homologado e executável original. Nada de hot-unload, Events::Remove ou restauração por DllMain.

### Política proposta de contenção, não autorizada nesta tarefa

A política FATAL do lab (TerminateProcess/kill-on-exit/watchdog) **não foi aplicada ao GTA**. Uma futura ferramenta deve exigir consentimento explícito e vinculado à sessão/PID antes de poder modificar código. Exemplo de consentimento a solicitar então: “Autorizo encerrar o gta_sa.exe desta sessão isolada, sem salvar, caso uma falha torne impossível confirmar rollback seguro”. Consequências de saída/crash do próprio debugger e kill-on-exit precisam integrar essa aprovação; não ativar esses mecanismos implicitamente.

Sem consentimento, a decisão é recusar a transação, não escrever e depois depender de autorização tardia. Após uma falha indeterminada não é seguro simplesmente desligar o debugger: destacá-lo pode liberar execução de instruções parciais. Um writer físico não será disponibilizado antes de resolver essa política e o caller. Nenhum comportamento de término do GTA está implementado aqui.

| Gate | Situação |
|---|---|
| Mesmo ASI READY/PIN/wrapper no host | **Comprovado** nesta build |
| Transação externa all-stop/falhas | **Comprovada no host**, Slice 4/regressão |
| Caller diferido admitido no GTA | **Ausente**; REQUESTED sem PIN |
| ABI completa do wrapper no Hoodlum | **Não homologada**, sensibilidade ao CALL extra ainda não demonstrada |
| Collector/admissão/writer GTA | **Ausentes**; preflight somente evidências/read-only |
| Política FATAL GTA aprovada | **Não autorizada**; diferente do laboratório |
| Contador/thread físico e critérios N1/N2 | Pendentes; Alpha 0.3.6 aberta |

## Revisões e Git

- Architect: GO host/ASI cooperativo, NO-GO GTA; nome/exports não são prova universal de contexto, preflight não é collector remoto e shutdown não quiesce chamadas admitidas.
- Windows native: implementou e auditou o ASI; PIN confirmado/publicação final; wrapper preserva perfil x86/SSE, mas CALL extra/alinhamento/return address não estão homologados no motor.
- Reviewer read-only: sem bloqueador novo no escopo; exigiu explicitar shutdown sem quiescência e apontou lacuna de parsing de ASI real, coberta pelo teste adicional. Execuções/testes foram conduzidos pelo agente principal.

Baseline permanece `af3af1c90c61216dfc153a2f51b14f2368e2c8bb`; índice vazio, sem staging/commit. As propostas das Slices 1–4 estão nos relatórios anteriores. Para commits futuros autorizados:

- Arquivos exclusivos da Slice 2 (observer/scene/adapter/host/pipeline) podem ser selecionados por path, com seus hunks próprios dos CMakeLists.
- `native_counter_experimental.cpp`, `.def`, `native_counter_host.cpp`, `native_counter_ready.hpp` contêm alterações de mais de uma slice; **selecionar hunks**, não adicionar integralmente à Slice 1/4.
- Em `client/windows/CMakeLists.txt`: targets counter base → Slice 1; scene host → Slice 2; writer/host exports/flags lab → Slice 4; flags do ASI e biblioteca preflight → Slice 5.
- Em `tests/CMakeLists.txt`: counter base → Slice 1; pipeline/loopback → Slice 2; codec/transações → Slice 4; cenários ASI/preflight → Slice 5.
- Novos preflight.hpp/.cpp, testes preflight e este relatório são paths exclusivos da Slice 5. `docs/native-game-thread-integration.md` pertence à Slice 3; relatórios históricos ficam separados.

Antes de qualquer commit, revisar o índice selecionado e compilar cada estado intermediário. Nenhuma seleção foi aplicada aqui; builds, logs, PDBs, disassembly e qualquer arquivo de instalação ficam ignorados. O próximo trabalho exige decisão sobre o caller/política, não criação automática de entidades ou avanço de slice.
