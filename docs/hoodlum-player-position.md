# Alpha 0.2.3 — verificação da integração Hoodlum

## Estado

O usuário informou o primeiro smoke nativo de carregamento: GTA SA 1.0 US Hoodlum, Ultimate ASI Loader Win32 7.7.0, `InitializeASI` executado e identidade confirmada em `GrandMP.log`. A leitura da posição **não está ativa**. Nenhum hook, chamada de função do GTA ou leitura de suas estruturas foi incorporado neste slice.

O arquivo `D:\Jogos\GrandMP-Smoke\gta_sa.exe` teve seu SHA-256 conferido localmente, em modo somente leitura: `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`.

## Fontes e evidências

| Fonte | Evidência | Limite e licença |
| --- | --- | --- |
| [plugin-sdk, `Events.h`](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/shared/Events.h) e [`common.cpp`](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/common.cpp) | `gameProcessEvent` após call-site `0x53E981`; `FindPlayerPed` em `0x56E210`; `FindPlayerCoors` em `0x56E010`. | Instalação e remoção auditadas e reprovadas para o ciclo de vida atual. Licença zlib; código não incorporado. |
| [sa-sdk, `sa10us_locators.hpp`](https://github.com/TsyVM/sa-sdk/blob/99787a3c92019de04d61d68bc5e1271233ed2e41/include/sasdk/game/sa10us/sa10us_locators.hpp) | Documenta locators para o MD5 Hoodlum correspondente. | Não comprova sozinho ABI de retorno e ciclo de vida do hook. Licença MIT; código não incorporado. |
| [gta-reversed, `World_Helper.cpp`](https://github.com/gta-reversed/gta-reversed/blob/c85e5dcd9b7aa088e9e7a80f647d98bcd14a140f/source/game_sa/World_Helper.cpp) | Descreve `FindPlayerPed` e `FindPlayerCoors`, inclusive a posição da entidade usada quando o jogador dirige. | O README exige Compact; layout ou ABI não foram transferidos ao Hoodlum. Licença não confirmada; código não incorporado. |
| [SAEncyclopedia, ciclo de frames](https://github.com/TsyVM/SAEncyclopedia/blob/b873c48bdb372e87030ba708ac68fdabe13b9716/C51-Executable-Lifecycle/04-frame-loop-subsystem-sequence.md) e [camada Hoodlum](https://github.com/TsyVM/SAEncyclopedia/blob/b873c48bdb372e87030ba708ac68fdabe13b9716/C0-Binary-Identity/01-the-hoodlum-layer.md) | Descrevem o loop e funções relocadas para `.HOODLUM`. | Um endereço de entrada não prova trampoline seguro. Licença não encontrada; código não incorporado. |

## ABI e ponto de chamada no executável local

O `dumpbin /disasm` do MSVC x86 foi executado sobre o arquivo local cujo SHA-256 consta acima. Esta evidência é específica desse binário; os endereços do Compact não foram transferidos. O [plugin-sdk nesta revisão, `common.cpp`](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/common.cpp) declara ambas as funções `__cdecl`.

| Entrada | Instruções e contrato observado |
| --- | --- |
| `FindPlayerPed`, `0x56E210` | `8B 44 24 04` lê `playerId` em `[esp+4]`. `test eax,eax; jge` deixa índices não negativos intactos; para `-1`, `movzx eax, byte ptr [0xB7CD74]` seleciona o jogador atual. O índice usa stride `0x190` e a tabela em `0xB7CD98`. Retorna em EAX o ponteiro do ped ou nulo, com `ret` simples e limpeza pelo chamador. Não é seguro desreferenciar o ponteiro após o retorno sem contrato de duração. |
| `FindPlayerCoors`, `0x56E010` | `8B 44 24 08` lê `playerId` em `[esp+8]`: há ponteiro de retorno oculto em `[esp+4]`. Com ped ausente, escreve zero nos três DWORDs de saída e retorna. Com ped presente, escolhe a posição da entidade ou do veículo segundo flags e ponteiro do ped; copia três DWORDs para o retorno. Um chamador do próprio jogo em `0x40E807` faz `push -1; push <endereço do retorno>; call 0x56E010; add esp,8`, confirmando retorno de 12 bytes por ponteiro oculto e limpeza `__cdecl`. A interpretação exata do ramo de veículo e a validade do ped durante transições exigem smoke. |
| `CVector` | O [cabeçalho do plugin-sdk](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/shared/game/CVector.h) define `RwV3d` com três `float` em offsets 0, 4 e 8 e valida tamanho `0xC`; `CVector` deriva dessa base e valida tamanho `0xC`. A desassemblagem da função copia exatamente três DWORDs. O alinhamento x86 natural de 4 bytes é uma inferência do tipo e do compilador, não uma medição do layout em memória do jogo. |
| `gameProcessEvent`, `0x53E981` | Bytes completos `E8 5A D5 FF FF`: `CALL rel32 0x53BEE0`, retorno em `0x53E986`. O call-site não empilha argumentos para essa chamada. O [evento do plugin-sdk](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/shared/Events.h) declara `CdeclEvent<AddressList<0x53E981,H_CALL>, PRIORITY_AFTER, ArgPickNone, void()>`. O alvo inicia com prólogo `sub esp,0Ch; push ebx; push esi; push edi`. O call-site fica em `0x53E920`, após processamento de timer; `0x53ECBD` chama essa rotina por um despachante. Isso fundamenta uma assinatura sem argumentos e uso no processamento de frames, mas não prova a disponibilidade do ped em menu, loading ou encerramento. |

## Bloqueio de instalação e encerramento

O [EventList do plugin-sdk](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/shared/EventList.h) instala o hook no primeiro `Add` via `Patch()`. `Remove`/`RemoveById` só apagam callbacks da lista; **não restauram o CALL original**. `PatchAll()` usa a versão reconhecida pelo SDK, mas não compara os bytes do call-site com `E8 5A D5 FF FF` antes da escrita. Além disso, `Patch()` marca `bPatched=true` antes de `PatchAll()`: se a versão do SDK não corresponder, o callback pode ser registrado sem patch e sem nova tentativa. O [injector/hooking.hpp da mesma revisão](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/injector/hooking.hpp) guarda os cinco bytes em `scoped_call`; `make_static_hook_dyn` põe o hook em uma lista estática, cujo destrutor restaura os bytes. O próprio código avisa que a restauração pode conflitar com outro hook no mesmo local. Uma cópia do SDK em cada ASI não compartilha necessariamente a lista ou a propriedade do patch.

Na Alpha 0.2.5, `InitializeASI()` mantém a guarda de inicialização única, mas o PIN de produção permanece bloqueado porque o contexto fora do loader lock não foi demonstrado. O runtime de diagnóstico é temporário e nenhum callback do jogo o referencia. O protocolo de callback ativo e a instalação concorrente do patch também não foram comprovados. Restaurar o patch durante o desligamento da DLL poderia ocorrer sob loader lock e sobrescrever uma alteração posterior de outro plugin. Assim, **não se registra callback nem se modifica memória** com o mecanismo atual.

O [código do Ultimate ASI Loader consultado](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/71ca8ae020b0be3ff6daec4739e105b51410070f/source/loader/plugins/plugins.cpp) chama `LoadLibraryW` e `InitializeASI`, sem `FreeLibrary` nesse caminho, mas essa revisão não foi comprovada como o fonte exato do loader Win32 7.7.0 instalado. Também não estabelece que outro componente não possa descarregar o ASI. O primeiro smoke confirma o carregamento, não o contrato de duração do módulo.

Para habilitar a leitura, é preciso definir uma propriedade única do patch, verificar os bytes originais em memória imediatamente antes da instalação, manter código e estado do callback válidos enquanto o CALL apontar para o ASI, especificar como impedir callbacks durante remoção e restaurar apenas se a instrução ainda for a instalada por GrandMP. Essa remoção deve ocorrer fora de `DllMain`, em ponto em que o jogo não esteja executando o CALL; caso contrário, um callback em trânsito ou uma instrução parcialmente reescrita permanece possível. É necessário validar também o momento do evento durante menu, loading, partida e encerramento. Uma simples opção de configuração para desabilitar callbacks não resolve a duração do patch. Um mecanismo próprio só deverá ser implementado após comprovar esses contratos e revisão especializada.

## Código testável

`client/windows/` contém `PlayerPositionLogGate`, política sem chamadas ao GTA. Ela aceita `integration_unavailable`, `player_absent` ou uma posição fornecida por futura integração verificada. Só permite log para Hoodlum, quando habilitada, com X/Y/Z finitos e intervalo monotônico mínimo de 1.000 ms. O componente permanece desconectado do ASI; posições construídas nos testes não são leituras do jogo. O core compartilhado não mudou.

## Condição para avançar

Antes de conectar a política a um callback, comprovar um ponto de entrada fora do loader lock para residência, resolver o bloqueio de instalação concorrente descrito acima, comprovar a fase do evento e a validade do ped, e adicionar opção própria de desabilitação por configuração local (desativada por padrão). O runtime da Alpha 0.2.5 não persiste após `InitializeASI`.

## Smoke físico de posição — pendente

Executar exclusivamente em `D:\Jogos\GrandMP-Smoke`, após a condição técnica acima e nova revisão do código. Confirmar: início normal; carregamento do ASI e identidade Hoodlum; menu e loading sem falhas; coordenadas após spawn; alteração ao caminhar; comportamento documentado no veículo; saída da partida sem acesso a objetos destruídos; encerramento normal. **Não copiar a build deste slice esperando coordenadas:** a integração permanece desativada. O usuário deve executar esse smoke antes de qualquer aprovação da Alpha 0.2.
