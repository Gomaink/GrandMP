# Alpha 0.3.4 — Native Execution Strategy (Slice 2)

## Decisão

**GO** para a ponte lógica `RemotePlayerScene` → `QueuedRemotePlayerAdapter` → backend simulado e para testes sem o GTA. **NO-GO** para chamar `pump()` no jogo, instalar callbacks/hooks/detours, escrever memória ou criar entidades. Não existe ainda um contexto de execução nativa homologado.

## Estratégias avaliadas

| Caminho | Valor e compatibilidade | Riscos/condições | Decisão |
| --- | --- | --- | --- |
| `plugin-sdk` `Events::gameProcessEvent`, revisão `15f15b60bbf74c106e1b496ff92c98764abf4605` | É o candidato funcional mais próximo do ciclo do jogo. A documentação existente registra o call-site `0x53E981`; no Hoodlum verificado os bytes observados foram `E8 5A D5 FF FF`, destino `0x53BEE0`. | O mecanismo altera um CALL interno; não há prova de quiescência, propriedade exclusiva dos bytes, compatibilidade da revisão/evento com cada build Hoodlum, nem duração/remoção segura de callbacks. Registro pode ocorrer fora de `DllMain`, mas isso não prova loader-lock livre nem thread segura. Conflitos com outros ASIs continuam possíveis. | **Prioridade para estudo posterior**, não para instalação. Validar passivamente o ciclo de chamada e depois desenhar experimento opt-in de contador, com revisão separada. |
| Inicialização diferida via Ultimate ASI Loader 7.7.0 / `InitializeASI` | `DontLoadFromDllMain=1` fornece o caminho documentado de inicialização diferida. O smoke anterior confirmou execução de `InitializeASI` e coincidência do TID com a janela nessa sessão. | Isso não prova loader-lock livre, contexto de frame, residência contra unload externo, nem a segurança de instalar patch durante execução. A ausência de frames `Ldr` e TIDs coincidentes não satisfaz esses gates. | Usar apenas como entrada de configuração/log. **Não é executor de comandos do motor.** |
| `SetTimer`/`TimerProc` | Funcionou no host Win32 com fila de mensagens isolada. | Depende de despacho de mensagens e não estabelece afinidade com atualização do motor; timer do host não observa objetos do GTA. | Rejeitada para acesso ao motor. |
| Detours v4.0.1 sobre `CGame::Process` | A análise anterior considera a entrada `0x53BEE0` e trampoline candidatos para estudo, evitando alterar o CALL interno. | A inscrição de threads da transação não cobre necessariamente todas as threads; a análise existente identificou falhas de contexto/retorno sem garantia fail-closed, e Detours não resolve residência, loader lock, colisões ou shutdown. | Alternativa secundária, também **NO-GO**. |

## Evidências existentes e limites

- Executável de referência: GTA SA 1.0 US Hoodlum, SHA-256 `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`.
- `InitializeASI` foi executado pelo ASI Loader; TID igual ao da janela principal foi observado numa sessão. Não houve medição confiável do loader lock nem prova de que esse TID seja o contexto de atualização do jogo.
- A inspeção estática anterior registrou a instrução do CALL e a entrada candidata de `CGame::Process`; a documentação ressalta que isso não valida ciclo de vida nem instalação concorrente.
- Revisões técnicas existentes: `docs/native-adapter-foundation.md`, `docs/verified-game-update-hook.md`, `docs/experimental-callback-decision.md` e `docs/runtime-evidence-windbg.md`. Este slice não repetiu a desassemblagem nem executou GTA.
- Evidência de fonte adicional: nessa revisão do plugin-sdk, `Events.h` declara o evento SA como `CdeclEvent<AddressList<0x53E981, H_CALL>, PRIORITY_AFTER, ..., void()>`; `EventList.h` chama `Patch()` no primeiro `Add`, constrói `injector::scoped_call` para o endereço e seus métodos `Remove` apenas apagam callbacks das listas. O código mostrado não sincroniza registro/remoção com dispatch. Isso confirma mecanismo e risco, não compatibilidade/runtime safety no Hoodlum.
- O Ultimate ASI Loader 7.7.0 e o plugin-sdk são dependências de pesquisa, não foram incorporados ao produto neste slice.

## Adapter e ciclo de vida

`RemoteRoster` e `RemotePlayerScene` continuam donos de identidade, sequência, interpolação e teleport. O adapter permanece limitado a comandos por valor e o backend simulado. `create`/`update` recebem no máximo três falhas consecutivas antes de fail-closed e limpeza no owner thread. Se o backend ficar indisponível durante shutdown, o estado permanece `stopping` até ele voltar; não declaramos limpeza que não ocorreu. `remove/removeAll` são síncronos `noexcept`, idempotentes e retornam confirmação booleana. Três falhas de limpeza deixam o adapter em `cleanup_failed`; `retryCleanup()` solicita nova tentativa explícita. Backend nativo futuro precisa cumprir esse contrato de forma limitada e não bloqueante; a implementação atual chama backend sob lock, portanto produtores e shutdown podem esperar a chamada terminar; isso ainda não é apropriado para integração de motor sem demonstrar operações limitadas/não bloqueantes ou mover chamadas para fora do lock. `create()` que falha não pode deixar entidade parcial; `update()` aplica pose absoluta; remoções repetidas precisam ser idempotentes.

## Próximo experimento mínimo

1. O usuário executa manualmente o procedimento passivo WinDbg de `docs/runtime-evidence-windbg.md`, usando instalação isolada, identificando símbolos/caminhos e registrando stack/TID/estado verificável do loader lock na entrada de `InitializeASI`.
2. Observar uma chamada de `CGame::Process` no menu e outra durante a partida, com breakpoint temporário controlado, registrando base, bytes, thread, retorno e stack. Não chamar funções internas, ler estruturas ou alterar memória persistente.
3. Avaliar os resultados e a estratégia de instalação/residência em revisão própria. Mesmo resultado favorável não autoriza automaticamente hook.
4. Somente após autorização posterior, considerar um ASI experimental separado cujo callback apenas incremente contador e encaminhe o original; sem `pump`, leitura do jogador ou criação de entidade.

O procedimento existente pode interromper temporariamente o processo no breakpoint e o debugger pode alterar estado transitório de execução. Encerramento: limpar breakpoints, continuar ou destacar o debugger; se travar, confirmar que não ficou parado num breakpoint e encerrar manualmente a instalação isolada como último recurso.

## Revisões

- Architect: GO para contrato/simulação; recomenda limites de tentativas e explicitar que backend não pode bloquear. NO-GO para chamar o adapter no GTA.
- Windows native: recomenda `plugin-sdk::Events::gameProcessEvent` como candidato prioritário futuro, mas NO-GO para instalação; a inscrição patcha o CALL, `Remove` não restaura o patch, e não há sincronização demonstrada contra dispatch.
- Reviewer independente: GO condicionado para a ponte simulada após revisão do contrato de falha/limpeza; NO-GO para integração nativa.

Referências primárias: [plugin-sdk `Events.h` na revisão fixa](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/shared/Events.h) e [plugin-sdk `EventList.h` na mesma revisão](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/shared/EventList.h).
