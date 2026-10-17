# Alpha 0.2.4 — fundamento do ciclo de vida de hooks

## Decisão e limites

O GrandMP continua **sem hook no GTA**. `InitializeASI()` agora possui um `WindowsClientRuntime` de duração do módulo, criado fora de `DllMain`, que detém o adaptador, a identidade detectada, a política de posição desativada, o registro de callback e um `HookManager` sem backend de patch. O adaptador não é mais um objeto temporário da pilha de `InitializeASI()`. O runtime não chama `FindPlayerPed` ou `FindPlayerCoors`, não registra um callback do jogo e nenhum destrutor escreve memória executável. O core, Android e servidor permanecem sem dependências de GTA.

A estratégia escolhida para este slice é **propriedade persistente do estado enquanto o módulo estiver carregado**, com diagnóstico lógico desativado e sem hot reload suportado. Isso corrige a duração temporária do adaptador; não prova residência irrevogável do ASI quando um futuro CALL apontar para ele. O estado armado é inalcançável pelo bootstrap atual. O retorno `false` do adaptador indica ausência de integração ativa, mesmo quando o fingerprint é reconhecido; a fase `diagnostic_only` do runtime distingue esses fatos.

## Ultimate ASI Loader Win32 7.7.0

| Classe de evidência | Resultado |
| --- | --- |
| [Release oficial v7.7.0](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v7.7.0) | Aponta a tag `v7.7.0` e commit `38c04fd`. Licença MIT. |
| [Fonte dessa tag, `source/dllmain.cpp`](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/v7.7.0/source/dllmain.cpp) | `FindFiles` (aprox. linhas 609–649) usa `LoadLib`/`LoadLibraryW`, procura `InitializeASI` e chama a exportação. Esse caminho não chama `FreeLibrary` para o ASI. `LoadEverything` (744–754) tem guarda de execução única; não foi encontrado hot reload automático nessa tag. `DontLoadFromDllMain=1` adia o carregamento para uma chamada originada pelo jogo; `0` carrega no `DllMain` (2395–2454). `CustomFreeLibrary` (963–969) protege o próprio módulo do loader, não um ASI de `FreeLibrary` externo. |
| Binário da instalação isolada, lido sem modificar | `D:\Jogos\GrandMP-Smoke\dinput8.dll`: 1.699.840 bytes, VersionInfo `7.7.0.0`, nome `Ultimate-ASI-Loader-Win32`, SHA-256 `29FEE0AA46414864AD1F65B2689B470BFB2CF2F09A94E2AFD271E68F2E16743B`. O `global.ini` instalado contém `DontLoadFromDllMain=1`. Esse binário não foi comparado byte a byte com o asset oficial da release. |
| Smoke anterior informado pelo usuário | O loader chamou `InitializeASI` e o log confirmou Hoodlum. Isso demonstra a entrada no ambiente testado, não o contrato de unload. |

A fonte v7.7.0 sustenta que o loader mantém a referência obtida por `LoadLibraryW` no fluxo normal e não recarrega o ASI automaticamente. Ela **não impede outro componente de chamar `FreeLibrary`**, nem demonstra que a DLL local é idêntica ao asset oficial. A versão corrente consultada antes (commit `71ca8ae`) organiza o carregamento em `source/loader/plugins/plugins.cpp` e não serve como prova do comportamento exato de 7.7.0. A função [`GetModuleHandleExW` com `PIN`](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw) é uma alternativa de residência até o término do processo; não foi adotada neste slice porque exige revisão explícita de retenção do módulo e da política de encerramento.

Uma validação isolada futura, sem GTA, pode usar um processo hospedeiro descartável, o asset oficial 7.7.0 verificado por hash e um ASI sentinela que registre attach, `InitializeASI` e detach. O hospedeiro observaria o carregamento normal, encerramento do processo e uma tentativa controlada de `FreeLibrary` feita por outro módulo. Esse teste mediria o comportamento da build exata, mas ainda não provaria segurança de um patch de instrução no GTA.

## HookManager sintético

`client/windows/` contém `HookManager`, `CallbackGate` e a interface `ICallSite`. Não existe implementação de `ICallSite` para memória do processo GTA; apenas `ControlledSite` em `tests/` fornece memória controlada. Os cinco bytes de `CallBytes` são dados de teste. A interface `exchangeIfEqual` representa uma transação sintética; ela **não garante escrita atômica de cinco bytes na arquitetura x86**.

| Transição | Regra |
| --- | --- |
| `idle -> installed` | Revalida `ExecutableInspection` PE32/i386, tamanho e SHA-256 completo pelo reconhecedor Hoodlum, além de exigir evidências externas explícitas de residência e quiescência, objetos válidos e igualdade com os bytes originais imediatamente no backend. Instalação duplicada é recusada. |
| `installed -> logically_disabled` | Bloqueia novos trabalhos diagnósticos. O site e o callback continuam registrados. Um callback que já entrou pode terminar. |
| `logically_disabled -> idle` | Somente com quiescência externa, sem callback em andamento e troca condicional dos bytes escritos pelo GrandMP pelos originais. Não há caminho de restauração a partir de `installed` enquanto o diagnóstico estiver ativo. |
| `logically_disabled -> conflict` | Se os bytes foram alterados por terceiro, não restaura, conserva site e callback, e exige que o módulo continue residente. Outro hook pode ter encadeado para código GrandMP. |
| `idle/logically_disabled -> uncertain` | Um backend que reporte escrita parcial ou resultado desconhecido deixa o registro retido. Não há rollback automático nem autorização para unload. |

Falha sem escrita deixa o estado `idle`; falha com efeito incerto conserva os objetos e bloqueia nova instalação. `HookManager` serializa suas transições com mutex. `CallbackGate` serializa entrada, desativação e saída, rejeitando reentrância e permitindo verificar callbacks em andamento. O `Lease` mantém **o gate**, não o código ou as capturas do futuro callback, vivo enquanto o callback está ativo. `mustRemainResident()` continua verdadeiro em conflito ou incerteza. Nenhum destrutor do gerenciador restaura bytes: destruir um gerenciador com registro ativo termina o processo explicitamente, em vez de soltar os objetos de forma silenciosa. Essa é uma **trava provisória do modelo sintético**; um hook real armado no runtime estático faria o processo terminar durante o descarregamento normal, portanto exige outro protocolo antes da integração. Os testes de conflito e escrita incerta retêm seus gerenciadores até o fim do processo CTest isolado; um teste em subprocesso exige código de saída 73 da guarda do destrutor. Essa guarda não é uma estratégia de unload para o GTA.

Os enums `ResidenceEvidence::externally_established` e `QuiescenceEvidence::externally_established` são **declarações fornecidas pelo chamador**, não provas produzidas pelo código. Os testes podem fornecê-las em memória controlada; o ASI não fornece essas declarações e não chama `install`. Para um backend real, comparar bytes antes de restaurar é necessário, mas insuficiente: threads podem estar executando a instrução ou o callback, e uma escrita de cinco bytes pode ser observada parcialmente. A instalação também precisa de quiescência. Não se deve usar esta interface com memória executável antes de projetar e revisar um protocolo real de sincronização e residência.

## Critérios que ainda bloqueiam o hook real

1. Demonstrar residência do código enquanto qualquer caminho, inclusive hook de terceiro encadeado, puder chamá-lo. A referência mantida normalmente pelo loader 7.7.0 não é garantia contra unload externo.
2. Definir o momento de instalação e eventual retirada com threads do GTA quiescentes, sem callbacks em trânsito e sem escrever instruções parcialmente visíveis. `DllMain` não será usado para restauração.
3. Definir propriedade e convivência com plugins que modifiquem o mesmo CALL. Em conflito ou instalação parcial incerta, não descarregar o módulo nem sobrescrever bytes alheios.
4. Validar fases menu/loading/spawn/saída e pré-condições de ped e entidade no callback real. Manter diagnóstico desativado por padrão em configuração própria antes de qualquer integração.

Os testes CTest desta etapa cobrem transições e memória sintética, inclusive concorrência de callback, mas não substituem smoke físico nem autorizam patch. A investigação da ABI Hoodlum permanece em [hoodlum-player-position.md](hoodlum-player-position.md).
