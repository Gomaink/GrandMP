# Alpha 0.3.4 — Native Adapter Foundation

## Escopo

Este slice conecta a `RemotePlayerScene` a um contrato de backend de entidades,
sem implementar chamadas ao GTA, criar peds, alterar memória ou ativar hooks.
O backend simulado valida o ciclo de vida fora do jogo. `RemoteRoster` continua
sendo a fonte de identidade/sequência e `RemotePlayerScene` continua dona da
interpolação de 180 ms e da decisão de teleport. O protocolo continua enviando
somente XYZ.

## Contrato e execução

`IRemotePlayerEntityBackend` define disponibilidade, criação, atualização,
remoção individual e limpeza total. `QueuedRemotePlayerAdapter` implementa o
sink atual da cena e transforma seus callbacks síncronos em comandos por valor.
O backend referenciado deve durar mais que o adapter. Chamadas de backend só
ocorrem em `pump()` e no primeiro thread que o executa; chamadas posteriores de
`pump()` em outro thread não executam trabalho. Produtores podem submeter
comandos de forma concorrente.

A fila tem capacidade fixa de 256 operações. Atualizações repetidas do mesmo
jogador são coalescidas e podem ser descartadas quando a fila está cheia.
Operações estruturais que não podem ser enfileiradas levam a ponte ao estado
`stopping`, limpam comandos antigos e fazem `pump()` solicitar `removeAll()` no
thread proprietário. O limite de representações ativas é o limite já existente
de 63 jogadores remotos. Sequências antigas são descartadas; `despawn` antes de
`spawn` cancela a intenção pendente. Um respawn enquanto há remoção pendente
preserva `remove` antes de `create`.

Estados: `cold -> ready -> stopping -> stopped`, com `cleanup_failed` após três falhas de `removeAll()` e retorno explícito `cleanup_failed -> stopping` por `retryCleanup()`. A inicialização pode ser encerrada diretamente como `cold -> stopped`. `resetAll()` cria uma
fronteira de geração, elimina comandos anteriores e agenda limpeza da sessão.
`shutdown()` fecha produtores ao adquirir o lock (pode esperar uma chamada de backend concorrente); a limpeza precisa ser drenada por
`pump()` no thread proprietário enquanto o backend ainda estiver disponível.
Se o backend estiver indisponível, a limpeza fica pendente e pode ser tentada
novamente quando voltar. O proprietário deve parar/desanexar produtores, chamar
`shutdown()`, drenar o `pump()` no thread correto e só então destruir backend e
adapter. O destrutor não chama APIs de backend porque sua thread pode ser
inadequada.

O adapter não decide quando é seguro chamar o motor. Em particular, não se deve
ligar `pump()` a `ObserverNetwork::tick()`: esse tick ocorre no ciclo do observer,
não em um contexto de atualização do GTA comprovado. O backend nativo deve
permanecer inativo enquanto não houver thread-affinity e ciclo de vida
homologados. Rotação, skin/modelo, animação, mundo e interior ficam fora do
contrato atual até existir uma fonte/protocolo apropriados.

## Evidência nativa e decisão

O smoke prévio validou somente leitura externa e eventos lógicos de cena. A
documentação existente registra o candidato `gameProcessEvent` do plugin-sdk na
revisão `15f15b60bbf74c106e1b496ff92c98764abf4605`, relacionado ao CALL em
`0x53E981` que no executável Hoodlum identificado foi observado como
`E8 5A D5 FF FF` com destino `0x53BEE0`. Isso não prova um ponto de chamada seguro,
instalação concorrente segura, disponibilidade do jogador nem ciclo de vida de
callbacks.

O `InitializeASI` via Ultimate ASI Loader 7.7.0, a configuração
`DontLoadFromDllMain=1` e a igualdade observada de TIDs não bastam para provar
ausência do loader lock ou contexto de frame seguro. Não há ainda prova de
residência do runtime durante callbacks nem estratégia homologada para instalação
e encerramento. Assim:

- **GO:** contrato portátil, fila, backend simulado e testes sem GTA.
- **NO-GO:** `pump()` chamando o motor, hooks, escrita de memória ou criação de
  entidade no GTA.

### Portões para integração nativa futura

1. Registrar sob WinDbg o contexto e a call stack efetiva de `InitializeASI`,
   incluindo o estado do loader lock por método confiável, no loader/configuração
   identificados.
2. Observar `CGame::Process` no Hoodlum em menu e partida; registrar base,
   instruções, thread e stacks sem inferir thread segura apenas por TID.
3. Demonstrar residência do módulo e estado durante callbacks e encerramento sem
   callback pendente para memória descarregada.
4. Revisar uma estratégia de integração que preserve a função original, tenha
   propriedade de patch clara, lide com concorrência e falhe sem afetar o jogo.
5. Somente depois, aprovar separadamente um smoke limitado de contagem de
   chamadas. A leitura de jogador e criação de ped exigem validações adicionais.

## Testes

`grandmp_remote_player_adapter_tests` usa backend em memória integrado à cena. Os casos cobrem spawn/poses/despawn, ordenação, sessão substituída, teleport, indisponibilidade/retomada, falhas transitórias e persistentes de create/update/remove, falha de removeAll, retry explícito de limpeza, shutdown com comandos pendentes, fila cheia, reentrância, concorrência e afinidade do thread proprietário. São contratos simulados; não demonstram segurança dentro do GTA.

O adapter aplica o limite existente de 63 representações. A 64ª criação leva a fail-closed e solicita limpeza no próximo `pump()` do thread proprietário.

## Falhas e limpeza

`create()`, `update()` e `remove()` são tentados novamente no próximo `pump()`. Após três falhas consecutivas, o adapter entra em `stopping`, descarta comandos pendentes e agenda `removeAll()` no thread proprietário. Se `removeAll()` falhar três vezes, o estado passa a `cleanup_failed`; o proprietário precisa chamar `retryCleanup()` explicitamente. O estado `stopped` só é informado após `removeAll()` confirmar sucesso. `resetAll()` cancela a geração anterior e agenda uma limpeza.

Se `available()` continuar falso durante shutdown, a limpeza permanece em `stopping`; não é seguro chamar um backend indisponível nem declarar os recursos limpos. O proprietário deve manter adapter/backend vivos e chamar `pump()` de novo no thread correto quando houver recuperação, ou encerrar o processo que possui esses recursos. O destrutor não força limpeza em uma thread arbitrária.

`remove()` e `removeAll()` são síncronos, `noexcept`, idempotentes e retornam `true` somente quando a representação está ausente. `create()` que retorna `false` não deixa entidade parcial (e criação por ID é idempotente); `update()` aplica pose absoluta e pode ser repetido. Todas as chamadas do backend são feitas sob o lock recursivo para preservar reentrância e estado in-flight. Assim, produtores e `shutdown()` podem esperar uma chamada concorrente de backend terminar; qualquer backend futuro precisa ser limitado e não bloqueante, sem aguardar um thread que possa entrar no adapter. Esta condição não foi comprovada para o motor do GTA.

## Validação manual já existente

Para repetir somente a validação lógica anterior, use o roteiro em `remote-player-foundation.md`: servidor e cliente sintético, depois `grandmp_position_observer.exe --connect 127.0.0.1:7777 --remote-scene-preview`. Confira eventos de texto spawn, pose e despawn. Esse procedimento não testa o backend simulado nem cria personagem visível. A criação de sessão nova após fail-closed requer uma nova cena/adapter sincronizada com o roster atual, conforme o fluxo descrito nesta página.