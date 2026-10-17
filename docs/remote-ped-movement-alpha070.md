# Alpha 0.7 / Slice 0 — um deslocamento remoto relativo, aguardando revisão

HEAD master/eee23777986baebe8849bcd2090b6f298aef6f3e; trabalho Alpha 0.4/0.5/0.6 ainda não commitado e preservado. Closeout separado: `build/alpha07-review/ALPHA06-CLOSEOUT.md`; hashes dos logs e binários em baseline.json/manifest.json. Confirma PID 18208, PED 1A28B134, remoteId 15806244455836936837, geração 2, criação 1835/remoção 2688, confirmação visual/estabilidade pelo operador. Restauração conferida por leitura: ACTIVE ausente, ASIs 0.5/0.6 ausentes, GrandMP.asi e contador restaurado com hashes esperados. Nenhuma escrita na pasta GTA.

## Arquitetura encontrada e decisão

UDP v1 transmite posições absolutas XYZ float, sem origem relativa. ObserverNetwork::sendPosition envia coordenadas Windows sem transformação; servidor replica valores e sequência sem mapear coordenadas. O sintético real gera X=100+(seq%1000)/10, Y=200, Z=30, a cada 200 ms (aprox. +0,5 unidade/s em X até o wrap). Isso não é o local de CJ.

RemoteRoster recusa identidades/sequências inválidas/antigas; Scene só emite spawn após primeira posição válida. Move recebe posição **interpolada em 180 ms**, inclusive várias amostras com a mesma sequência de destino. Saltos acima de 35 unidades não interpolam. Não há API nova nem mudança de protocolo/rede/Scene/sintético.

Alpha 0.6 armazenava somente a última pose no sink; Controller consumia revisão apenas de lifecycle. A Alpha 0.7 acrescenta, somente sob GRANDMP_REMOTE_PED_MOVE_VARIANT, a primeira pose imutável e sampleRevision monotônica para a publicação ordenada da Scene. Isso preserva a âncora mesmo se a rede avançar durante streaming. Mutex é liberado antes de qualquer operação do motor.

Fluxo: UDP → ObserverNetwork/RemoteRoster/Scene → IntentSink de valores → snapshot → fase pós-população aprovada → wrapper do Controller existente → Movement/NativeEngine. O worker não recebe ponteiros do PED/controlador. Todo método do fake engine verifica identidade da thread consumidora, inclusive logs/probes/SetTask/remoção; teste thread e loopback exercitam a separação. Não é prova de gameplay.

## Um comando físico

Após ped_created com ownership demonstrado, combina primeira pose remota com posição segura passada ao AddPed, confirmando pose real a até 0,25 m dessa âncora. Primeira pose não move. Amostras posteriores precisam corresponder ao playerId/geração/lifecycle e ter sequência superior à inicial, ordem de publicação não obsoleta e valores finitos. Atualizações interpoladas na mesma sequência são aceitas em sua ordem de publicação, sem recalcular a âncora.

O comando é `local_spawn.XY + (remote_sample.XY - first_remote.XY)`. Não normaliza para 4 m e não usa XYZ absoluto UDP como destino. Delta horizontal abaixo de 2 m aguarda (um único log); primeiro delta entre 2 e 4 m consome a tentativa para toda a vida desta ASI. Maior que 4 m, não finito ou altura remota acima de 1 m rejeita e consome, sem clamp/retry. Delta Z é guarda; a altura do destino vem do solo local, sem sincronização vertical arbitrária.

Antes de SetTask, confirma âncora local/ownership novamente, destino horizontal a no máximo 4 m da pose real e proximidade de CJ, consulta solo e esferas num corredor com espaçamento até 0,5 m, altura total até 1 m e degrau até 0,5 m. Ignora somente o PED próprio; CJ e demais entidades permanecem obstáculos. Probe usa exatamente as funções/ABI já aprovadas. Destino é calculado uma vez. Pacotes posteriores não escrevem transform nem atribuem novas tarefas.

Movement existente ganhou um parâmetro de distância com padrão **4 m**: F7 permanece igual. Reutiliza os mesmos critérios de início real (>=0,15 m), descontinuidade, chegada (<=0,6 m XY/0,75 m Z, estacionário 300 ms) e timeout 20 s. SetTask aceito não é chegada. Não há nova implementação de task manager.

NativeEngine e contrato de tarefa preservados, exceto escolha do gate da variante: allocator 0x61A5A0; ctor 0x668120, thiscall/seis argumentos/0x28 bytes; manager intelligence+4, owner+0x2C, slot 3; SetTask 0x681AF0, transferência de ownership. Slots primários ocupados continuam recusados, sem sobrescrever. Não se retém/desreferencia/deleta o task após transferência. Endereços, regiões, vtable, pool e ABI continuam em ped_smoke_move_native.hpp, que não mudou.

Leave sempre roda pelo Controller/lifecycle aprovado antes da observação da tarefa. Remove somente o PED autenticado, inclusive com tarefa ativa; destruição nativa encerra suas tarefas. Movimento pendente/ativo vira remote_move_cancelled, sem failed por cancelamento normal. Não lê pose após remoção. Perda de ownership/código é terminal, sem remover identidade desconhecida. Não há segunda tarefa após reconnect (o limite é por processo/ASI, não por geração).

Logs: remote_move_position_received (primeira/candidata, não a cada pacote), anchor_set (coordenadas das duas âncoras), requested (destino relativo), task_assigned, started, arrived, cancelled, rejected e failed. Runtime correlaciona remote, generation, seq/sample, PED, PID/thread e callback. Logs antigos F6/F7/F8 e Alpha 0.6 preservados.

## Gates e isolamento

| Target | Spawn | Remoto | Movimento remoto | Estado |
| --- | --- | --- | --- | --- |
| grandmp_ped_smoke_move_enabled | 1 | 0 | 0 | Alpha 0.5 preservada |
| grandmp_remote_ped_enabled | 1 | 1 | 0 | Alpha 0.6 preservada, sem movimento remoto |
| grandmp_remote_ped_move_experimental | 0 | 0 | 0 | Nova ASI fechada, compilada explicitamente |
| grandmp_remote_ped_move_enabled | 1 | 1 | 1 | Futuro, apenas configurado para verificar CMake |

GRANDMP_BUILD_REMOTE_PED_MOVE_ENABLED é OFF por padrão, EXCLUDE_FROM_ALL, saída própria remote-ped-move-enabled. Gate exige simultaneamente GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1, GRANDMP_REMOTE_PED_PHYSICAL_BUILD=1 e GRANDMP_REMOTE_PED_MOVE_PHYSICAL_BUILD=1; ambiente sozinho não habilita. Nenhum target antigo recebe o define de variante/movimento remoto. Nome/identidade/log exclusivos: GrandMPRemotePedMoveExperimental.asi/.log. Não coinstalar com outros experimentos que usam 0x53C05C.

## Arquivos e validação

- ped_smoke.cpp: seleção condicionada de header/identidade/log/gate da nova variante; hook/thunk/caller/publicação/PIN intactos.
- ped_smoke_move.hpp: distância opcional com padrão 4; lógica de observação e ABI nativa intactas.
- remote_ped.hpp: const accessor da entidade; campos de âncora/revisão só na variante nova; publicação sob mutex.
- remote_ped_runtime.inl: wrapper/engine da variante, captura da posição segura de spawn, probes aprovados e logs correlacionados.
- novos remote_ped_move.hpp/.def: consumidor único, tradução relativa, corredor e export da ASI isolada.
- CMake Windows/testes: targets isolados, casos focados e oito combinações dos três gates.
- novos remote_ped_move_tests.cpp e remote_ped_move_gates_tests.cpp; remote_ped_loopback.py mantém modo antigo e ganha janela de 8 s para o movimento sintético.

Build VS 18 2026 Debug/Win32, novo cache build/alpha07-remote-move-win32, BUILD_TESTING=ON, todas as opções físicas OFF. Única ASI compilada foi a nova fechada; fontes/targets homologados não foram reconstruídos em seus caches. Primeiro build detectou define de variante ausente no host de gates; corrigido no host. Build final sem erros/warnings, comandos e logs em build/alpha07-review/.

CTest serial -j 1: **87/87 + 2/2 = 89/89 PASS**. São 51 regressões e 38 novos (28 unidades, 8 gates, loader fechado e loopback). Dois testes reutilizam os ASIs fechados anteriores via receitas específicas em reuse-closed, sem cópia/rebuild. Cobrem âncoras/coalescing, pequenos/válidos/máximos/oversize, NaN/Infinity, altura/solo/degrau/obstáculo, duplicatas/stale/geração/outro player/leave antigo, leave pendente/ativo, ownership/identidade, reentrância/tarefa ocupada/timeout/chegada/limite global, thread e F6/F7/F8/join/leave antigos.

Loopback real: servidor e cliente sintético sem mudanças → ObserverNetwork → Scene interpolada → IntentSink → Controller → task fake; assertions exigem uma criação/tarefa/início/chegada/remoção. Apenas o fake altera sua própria posição para simular caminhada; o runtime não escreve posição GTA. Loader real da nova ASI fechada recusa execução apesar de env opt-in, com zero callbacks. Nenhum GTA foi executado.

Artefato **fechado**: build/alpha07-remote-move-win32/client/windows/remote-ped-move/Debug/GrandMPRemotePedMoveExperimental.asi; PE32/x86 DLL, 616448 bytes; SHA-256 `FE6A3289647B0E8E7FA4F0041CC58C116CA576325D16DB2F4F53C73100BCE8C3`.

62 ASIs da baseline atual mantiveram hashes/datas; evidências/logs do smoke Alpha 0.6 mantiveram seus hashes. A ASI homologada 62E6E34FCC25A8E7A318123752F4F6EED2CDB6ECFDEB22ED1E4AF87C6931B134 está intacta. Código de endereços/thunk/ABI, UDP/Observer/Scene/servidor/sintético, executável e index Git preservados. Nenhuma alteração em scripts/backups/saves, supervisor HELD ou commit/stage/push. Closeout e patch incremental são separados.

## Riscos e futuro smoke (somente após revisão/autorização)

Ainda falta comprovar o movimento remoto real. Corredor é uma inspeção instantânea; tráfego/obstáculos podem mudar, tarefas do motor podem ocupar slots e a rota real não é garantidamente reta. Limite refere-se ao destino horizontal solicitado; não há controle por frame de trajetória. Timeout encerra observação, **não aborta automaticamente uma tarefa transferida**. Leave continua o cleanup aprovado do PED conhecido. Não há destrutor especulativo ou operação sobre entidade desconhecida. Persistem as limitações UDP v1/IDs aposentados da Alpha 0.6.

Após revisão, preparar/auditar separadamente a variante habilitada e obter autorização de instalação. Com GTA encerrado, procedimento do operador deve preservar/restaurar a instalação e usar apenas a ASI nova, sem coinstalação. Em área aberta, F6 arma a fase/receptor; iniciar servidor 127.0.0.1:7777 e sintético real. Confirmar ped_created/anchor_set; após aproximadamente 4 s de amostras, esperar um único requested/task_assigned, caminhada visível, started e arrived/parada; conferir coordenadas locais no log. Continuar recebendo pacotes sem segunda tarefa. Ctrl+C no sintético deve remover o mesmo PED. Leave durante caminhada deve ser outro smoke/processo, pois a tentativa é única. Não usar F7/F8 locais para comandar o remoto. Encerrar sem salvar e preservar evidência visual/logs/hashes.

Estado: **GO para revisão do incremento; NO-GO para instalação/smoke nesta entrega**. Variante física habilitada não compilada, nenhum hook físico aplicado.
