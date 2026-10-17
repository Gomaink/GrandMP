# Alpha 0.3.4 — Native Callback Readiness (Slice 4)

## Decisão

**NO-GO para inscrever um callback no GTA nesta etapa**, inclusive um callback que só conte chamadas. As observações físicas confirmam que, na sessão capturada, o loader chamou `InitializeASI` e que o call-site `0x53E981` transferiu controle para `0x53BEE0`. Elas não demonstram que a inscrição seja segura, que o código permaneça residente, nem que dispatch e shutdown possam ser coordenados.

O caminho prioritário para avaliação posterior continua sendo `plugin-sdk::Events::gameProcessEvent`, revisão `15f15b60bbf74c106e1b496ff92c98764abf4605`. Esta conclusão não autoriza implementar ou instalar o experimento.

## Evidência física consolidada

Fontes primárias: logs CDB em [`build/runtime-evidence/20261012-223706`](../build/runtime-evidence/20261012-223706/).

| Observação direta | O que demonstra | O que não demonstra |
|---|---|---|
| `01-initialize-asi.txt`: PID `0x5694`; breakpoint em `GrandMP!InitializeASI`, TID `0x49B8`, EIP `0x71C545F0`. A stack contém frames de `DINPUT8.dll` 7.7.0 entre a exportação e o jogo. | `InitializeASI` foi alcançado por uma chamada no caminho do Ultimate ASI Loader, na TID observada. | Não prova que o loader lock estava livre. A saída `!ntsdexts.locks` foi somente `Scanned 13 critical sections`, sem identificar o loader lock ou seu proprietário. Frames ausentes não são prova de lock livre. |
| `02-menu-callsite.txt` e `03-gameplay-callsite.txt`: em ambos, TID `0x49B8`, EIP `0x0053E981`, bytes `E8 5A D5 FF FF`; disassembly decodifica `call 0x0053BEE0`. Na entrada observada do destino: bytes `83 EC 0C 53 56 57`, EIP `0x0053BEE0`. | Nas duas paradas observadas, a instrução era um CALL completo e a execução chegou ao endereço destino; a TID era a mesma da captura de inicialização. | A captura rotulada “menu” pode ter ocorrido na transição para gameplay. Portanto, não comprova execução separada no menu e na partida, nem prova que o ponto represente todas as atualizações do jogo. A TID coincidente não prova thread-affinity segura. |
| Os dois scripts registram `Command file caused an implicit wait` e `HRESULT 0x80004005` após `t`; logo depois o transcript mostra EIP `0x0053BEE0`. | O destino foi observado após o single-step, mas o arquivo de comandos falhou nesse ponto. | Os comandos que viriam depois de `t` não são evidência de execução bem-sucedida. A coleta pretendida de contexto posterior pelo script está incompleta. |

O erro de `t` não foi corrigido neste slice: essa correção é opcional e exigiria um teste CDB com processo descartável. Não se repetiu a sessão física nem se alterou o launcher.

## Estratégia e propriedade do patch

Na revisão fixa do plugin-sdk, `Events::gameProcessEvent` associa o evento de prioridade `AFTER` ao CALL em `0x53E981`; o primeiro `Add` instala o patch por `injector::scoped_call`. O dispatcher preserva a chamada original e então executa os handlers `AFTER`. `Remove()` remove entradas das listas de callbacks, mas isso **não equivale a restaurar o CALL original**, nem demonstra que não há callback em andamento. O patch no código do jogo pode permanecer instalado depois da remoção lógica do handler.

Antes de qualquer registro futuro, ainda é necessário demonstrar:

1. **Ponto de instalação:** ocorrer fora do loader lock, ou usar inicialização diferida com contrato verificável; e provar que o call-site não está sendo executado concorrente à alteração.
2. **Propriedade/conflito:** conferir bytes imediatamente antes da instalação; detectar alteração de terceiros; definir quem é dono do patch e o que ocorre se outro plugin tocar no mesmo CALL. Validação prévia de bytes não elimina corrida entre validação e escrita.
3. **Dispatch concorrente:** demonstrar que patch e publicação do callback são atômicos em relação ao dispatcher, sem execução parcial, callback perdido ou chamada a estado não inicializado. A evidência atual não mostra a implementação sendo registrada.
4. **Residência:** garantir que DLL, código do callback e estado associado permaneçam válidos enquanto o CALL/dispatcher puder alcançá-los. O loader/versão/configuração observados não provam ausência de unload ou reload durante o processo.
5. **Shutdown:** não tratar `Remove()` como desinstalação. Não descarregar o módulo nem destruir o estado referenciado enquanto o patch puder despachar ou um callback estiver em voo. Uma estratégia residente até o fim do processo e desativação lógica exigiria prova explícita de residência; `PIN` não é substituto para os demais gates e não deve ser aplicado sob loader lock.
6. **Fluxo original:** validar no build exato do jogo que a rotina original é chamada exatamente uma vez e que o evento não altera registradores/stack ou comportamento observável além do contador.

O evento é executado no caminho do jogo indicado pelas capturas; inscrevê-lo também faz o dispatcher entrar nesse ciclo. Mesmo um contador deve ser atômico, limitado e sem alocação, logging, mutex ou chamadas ao backend. As observações atuais não provam que callbacks podem fazer trabalho mais amplo com segurança.

## RemotePlayerAdapter e mutexes

`QueuedRemotePlayerAdapter::pump()` estabelece o owner thread na primeira chamada e mantém `recursive_mutex` enquanto chama `available`, `create`, `update`, `remove` ou `removeAll` no backend. Chamá-lo de um callback nativo colocaria operações do backend no ciclo do jogo sob esse mutex; produtores e shutdown podem bloquear aguardando a operação. Um backend que espere outra thread pode introduzir atraso de frame ou deadlock por inversão de locks.

O experimento contador, se futuramente aprovado, não deve referenciar o adapter. Antes de qualquer integração do adapter, será necessário definir drenagem de comandos no owner thread, limites de duração, ordenação de locks e shutdown após produtores e callbacks em voo terem cessado. Não basta reutilizar `pump()` dentro do callback.

## Matriz de gates

| Gate para experimento contador | Estado | Evidência/pendência |
|---|---|---|
| Call-site e destino no Hoodlum | **Parcialmente GO** | Bytes e transferência observados nos dois logs; primeira fase “menu” ambígua e captura interrompida por `t`. |
| Contexto apropriado do jogo | **NO-GO** | Mesma TID e stack de jogo são observações, não prova de ciclo completo ou segurança de callback. |
| Loader lock livre na inscrição | **NO-GO** | Não determinado; `!ntsdexts.locks` não identificou o lock. |
| Instalação sem corrida e com ownership | **NO-GO** | Não há prova de quiescência, atomicidade, conflito ou tratamento de instalação parcial do patch do evento. |
| Residência de código/estado | **NO-GO** | Não há garantia demonstrada contra unload/reload pelo loader. |
| Remoção/shutdown sem callback pendente | **NO-GO** | `Remove()` não restaura o CALL nem prova ausência de dispatch/callback em voo. |
| Integração com adapter/backend | **NO-GO** | Mutex envolve chamadas ao backend; contexto e bloqueio não foram homologados. |

## Menor experimento posterior e critérios

Somente após fechar os gates de instalação, residência e dispatch, considerar um **ASI experimental separado**, explicitamente opt-in, em instalação Hoodlum isolada e sem outros ASIs não revisados. O primeiro callback não acessa GTA, adapter ou backend: apenas incrementa um `std::atomic<uint64_t>` com `memory_order_relaxed`; fora do callback, apresentar o contador em frequência baixa. A chamada original deve continuar exatamente uma vez.

**Aprovação:** fingerprint correto; instalação única; validação de bytes/propriedade; confirmação de que o callback executa na thread observada; contador cresce sem erros; jogo mantém menu, partida e encerramento normais; módulo/estado continuam residentes durante todo o despacho; nenhuma operação de remoção/unload deixa ponteiros executáveis pendentes.

**Rollback:** desabilitar o opt-in; fechar o jogo normalmente; somente com o processo encerrado, remover o artefato e configuração do ASI experimental. Não tentar restaurar manualmente o CALL, não executar hot-unload e não usar `Events::Remove()` como prova de remoção do patch. Qualquer byte inesperado, falha de instalação, callback em thread inesperada, falha de encaminhamento, travamento ou crash reprova o experimento; encerrar o teste, preservar logs e remover o artefato com o jogo fechado.

**Encerramento:** enquanto o patch permanecer instalado, callback e estado devem permanecer residentes. A desativação lógica pode impedir trabalho do callback, mas não torna seguro descarregar a DLL. A primeira experiência deve terminar pelo encerramento normal do processo, sem destrutor que altere código executável em `DllMain`.

## Pareceres

- **Architect:** NO-GO. Recomenda callback contador sem adapter; inscrição concorrente, residência, loader lock e remoção continuam sem garantias. Não chamar `pump()` no callback devido às chamadas de backend sob mutex.
- **Windows native:** NO-GO. Confirma bytes do CALL e chegada ao alvo, mas ressalta que menu não foi comprovado e a saída pós-passo não foi coletada. Recomenda fechar gates de loader lock, residência, propriedade/quiescência do patch e manter o adapter fora do contador.
- **Reviewer read-only:** NO-GO. Confirma que logs não resolvem loader lock, instalação concorrente, propriedade do patch ou unload; `t` gerou implicit wait, embora EIP no destino apareça depois.

Este slice não executou GTA, não instalou callback/hook e não alterou `GrandMP.asi`, rede, servidor ou Android.
