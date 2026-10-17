# Alpha 0.5 / Slice 1 — caminhada local, aguardando revisão

F7 usa a mesma detecção de borda do F8. Consome uma tentativa de caminhada por processo, sem repetir tarefa ao segurar/pressionar novamente ou em callback reentrante. F6/F8, identidade, streaming, criação, remoção, PIN, CALL e thunk da Alpha 0.4 permanecem iguais. F8 tem prioridade sobre F7 e encerra a observação antes de qualquer leitura posterior do PED removido. Perda de ownership é terminal; nenhuma tarefa recebe CJ como alvo.

A build padrão continua `physicalExecutionApproved=false`. O gate de movimento exige ambos os defines `GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1` e `GRANDMP_PED_MOVE_PHYSICAL_BUILD=1`; permanece fechado na padrão e no antigo target F6/F8. O novo target é independente, OFF por padrão e não foi compilado. Variável de ambiente não abre gates de build. A revisão estática, matriz de gates e resultados atualizados estão no [follow-up 1](ped-smoke-movement-followup1.md).

## Contratos conferidos por leitura do Hoodlum

Executável isolado: SHA-256 `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`, base `0x400000`. Não foi executado. Disassemblies delimitadas em `build/alpha05-ped-move-research/hoodlum-*.txt`; extração adicional e vtable em `hoodlum-task-evidence.txt`. Não distribuir esses dumps como código do projeto.

| Contrato | Evidência no executável admitido |
| --- | --- |
| `CTask::operator new(unsigned)` em `0x61A5A0`, cdecl | Carrega pool `[0xB744A8]` e salta para `0x61A500`; slots de `0x80`, bitmap com bit livre `0x80`, retorno nulo ao esgotar. Caller `0x66CEB7` pede `0x28` para esta tarefa. Não usa o heap CRT da ASI. |
| Construtor `0x668120`, thiscall | ECX é objeto; stack: moveState, `const Position&`, radius, moveStateRadius, overshoot, stopExactly. `ret 0x18`, EAX retorna objeto. Campos `+0x0C`, `+0x10`, `+0x1C`, `+0x20`, flags `+0x24`; vtable `0x86FDA8`; tamanho `0x28`. Caller original `0x66CED3–0x66CEFE` confirma seis argumentos. **Não passa o sétimo argumento NOTSA de Reversed.** |
| Caminhar e parar | MoveState `4`; radius `0.5`, moveStateRadius `0`, overshoot false, stopExactly true. Construtor limita raio mínimo de caminhada a `0.5` (`[0x858B8C]`). Vtable: `CreateFirstSubTask=0x66DC40`, `CreateNextSubTask=0x66DBA0`, `ControlSubTask=0x668570`. Primeiro cria GO_TO_POINT (`0x384`) via `0x6682D0`; ao terminar, cria STAND_STILL (`0xCB`). |
| `CPedIntelligence` e manager | `0x607168` guarda PED em `[intelligence]`; `0x607161` passa `intelligence+4` ao construtor do manager `0x6816A0`, que guarda dono em `+0x2C`. Caller `0x41C1AD–0x41C1C3` usa `[ped+0x47C]+4`, slot 3. Ambos os donos são revalidados em runtime, após ownership do PED. |
| `SetTask` em `0x681AF0`, thiscall | ECX=manager; stack tarefa, índice, bool; `ret 0x0C`. Guarda raiz em `[manager+index*4]`, cria subtarefas e pode destruir a tarefa imediatamente se a árvore for inválida. Transferência definitiva; a ASI só compara o slot depois, sem acessar/liberar a tarefa transferida. Slots 0–3 precisam estar vazios; DEFAULT (4) permanece sob o motor. |
| Cleanup nativo | Destrutor deletor `0x66DB80` → `0x668240` → destrutor complexo `0x61A3D0`, destrói subtarefa e retorna ao pool por `0x61A5B0`. Manager destrói suas raízes. Detour original Hoodlum `0x6816E5→0x403AAB→0x6816EA` zera o slot. F8 continua usando somente o lifecycle aprovado do PED. |
| Posição/direção | Matriz em `[ped+0x14]`, posição `+0x30`, forward `+0x10`; o próprio cálculo nativo em `0x66833C–0x66834C` usa esses campos. Leituras são feitas somente após referência/pool autenticados; nenhuma coordenada é escrita. |

As novas entradas, implementações diretas e vtable têm comparações adicionais com o arquivo admitido e suas proteções, antes de alocar. Não há alteração nos gates existentes nem alegação de verificar transitivamente todas as funções internas do motor. Falha de identidade após alocação/construção retém o resultado desconhecido e encerra a tentativa, sem destrutor especulativo. Depois de `SetTask`, nenhuma falha autoriza liberar/reinstalar a tarefa.

Fontes consultadas como referência factual, sem incorporar implementações nem DLL Compact: [Reversed: tarefa e lifecycle](https://github.com/gta-reversed/gta-reversed/blob/c65cfe85f90bfe80cdb3b82701891f376a3482ea/source/game_sa/Tasks/TaskTypes/TaskComplexGoToPointAndStandStill.cpp), [plugin-sdk: alocação](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/CTask.cpp), [SetTask](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/CTaskManager.cpp), [intelligence](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/CPedIntelligence.h) e [matriz](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/CMatrix.h). Plugin-sdk: autores GTA Community, licença zlib; nenhuma classe/layout de Compact é vinculada à ASI.

## Comportamento e limites

Destino 4 m à frente do PED, direção XY normalizada. O corredor é amostrado a cada 0,5 m: solo encontrado/finito, altura `ground+1`, desnível total ≤1 m e passo ≤0,5 m; esfera de raio 0,35 ignora somente o PED experimental. CJ continua contando como obstáculo. PED vivo, civil tipo 4, área exterior, fora de veículo; posição e destino ficam até 12 m de CJ e desnível até 3 m. Falhas específicas recusam a única tentativa, sem caminho alternativo ou relaxamento de guards.

Logs de transição: `move_requested`, `move_probe` (origem/destino), `move_task_assigned`, `move_started` (deslocamento real ≥0,15 m), `move_arrived` (distância XY ≤0,6 m, Z ≤0,75 m e posição estável dentro de 0,05 m por 300 ms), `move_rejected`/`move_failed` com motivo. Rejeições repetidas são suprimidas. Aceitação da tarefa não prova movimento/chegada; salto >1,5 m entre observações é recusado. Timeout de 20 s encerra a observação, sem reinstalar tarefa. F8 permanece disponível após falha da caminhada se a identidade ainda for comprovada.

A amostragem detecta obstáculos evidentes; não prova navegabilidade nem ausência de obstáculos futuros. Eventos/colisões do próprio motor podem interromper o andar. O timeout/proximidade não cancela uma tarefa já transferida: usar F8 para remover o PED pertencente ao experimento. Capacidade inicial de oito slots livres evita iniciar sob pressão imediata do pool, sem prometer reservas futuras. Pausa/loading suspende consultas, mas o timeout usa tempo decorrido. Nenhum caminho nativo novo foi executado no GTA; a confirmação visual de caminhada/parada ainda falta. Não foi identificado argumento/layout pendente nas chamadas diretas estudadas; revisão e smoke continuam obrigatórios.

## Build e testes

```powershell
cmake -S . -B build/alpha05-ped-move-win32 -G 'Visual Studio 18 2026' -A Win32 -DBUILD_TESTING=ON -DGRANDMP_BUILD_PED_SMOKE_ENABLED=OFF
cmake --build build/alpha05-ped-move-win32 --config Debug --target grandmp_ped_smoke grandmp_ped_smoke_tests
ctest --test-dir build/alpha05-ped-move-win32 -C Debug -R '^ped_smoke_' --output-on-failure
git diff --check
```

24/24 testes focados passaram: 16 anteriores + 8 de movimento, incluindo ABI x86 com callbacks descartáveis, comando único/reentrância, terreno/colisão/NaN, chegada real/estabilidade, salto, timeout, perda de ownership e F8 durante/depois do movimento. O teste de inicialização carrega apenas o artefato bloqueado em host descartável: nenhum callback ou engine call. Não valida execução das rotinas GTA nem todos os guards live de `assignMove`. Logs em `build/alpha05-ped-move-win32/ped-build.txt` e `ped-ctest.txt`.

Milestone Alpha 0.4 preservado separadamente em `build/alpha04-milestone-review/`: snapshot byte a byte, manifesto e `alpha04-milestone.patch` contra `eee2377`. O patch incremental desta entrega é `build/alpha05-ped-move-win32/alpha05-incremental.patch`; não stage a árvore inteira como Alpha 0.4.

## Futuro smoke — somente após revisão e nova autorização

1. Revisar código, ABI e ambos os gates; preparar futuramente uma variante explicitamente habilitada e conferir seu SHA. **Não instalar a build bloqueada desta entrega.**
2. Com GTA totalmente encerrado, preservar ASI/log anteriores e substituir somente `scripts/GrandMPPedSmoke.asi` pela futura variante aprovada, conferindo SHA de origem/destino e dos outros ASIs. Não alterar arquivos enquanto GTA estiver aberto.
3. Iniciar manualmente o GTA isolado com o opt-in existente. Em exterior plano e aberto, F6; confirmar `ped_created` e CJ/PED simultaneamente visíveis. CJ permanece perto e fora do corredor frontal do PED.
4. Pressionar/soltar F7 uma vez. Confirmar sequência até `move_arrived`, caminhada visual de aproximadamente 4 m e parada. Segurar/repetir F7 não pode atribuir outra tarefa.
5. F8: confirmar `removed`, desaparecimento somente do experimental e ausência de respawn. Em tentativa separada, testar F8 durante o andar. Encerrar sem salvar e preservar log/vídeo e hashes.

Entrega para revisão; nenhum GTA, cópia/instalação, hook físico ou commit/push executado. Artefatos ASI anteriores permanecem preservados.
