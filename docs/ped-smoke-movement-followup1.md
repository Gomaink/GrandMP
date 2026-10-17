# Alpha 0.5 / Follow-up 1 — revisão estática e gates

Baseline: `master`, `eee2377`, alterações Alpha 0.4/0.5 sem commit. `git status --short --branch`, `git log -5 --oneline` e `git diff --check` conferidos. Nenhum reset, clean ou stage. Milestone `build/alpha04-milestone-review/` preservado; manifesto desta revisão em `build/alpha05-followup1-review/`.

## Contratos: evidência direta Hoodlum

SHA-256 do executável lido: `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`, base `0x400000`. Reconciliados **5.113 prefixos de instruções em 12 arquivos locais** `build/alpha05-ped-move-research/hoodlum-*.txt`, incluindo `hoodlum-task-evidence.txt`, contra os bytes atuais. A extração ampla pode conter desalinhamentos após dados; para fronteiras de funções/ABI foram usados os dumps delimitados por entrada, não a decodificação linear ampla. Relatório pequeno: `hoodlum-static-verification.json`. Os dumps completos permanecem locais/ignorados pelo Git.

| Contrato | Bytes/instruções essenciais diretamente observados | Conclusão |
| --- | --- | --- |
| Alocador `0x61A5A0` | `8B 0D A8 44 B7 00` (`ECX=[0xB744A8]`); `E9 55 FF FF FF` (salta a `0x61A500`). Caller `0x66CEB7`: `6A 28`, chamada e `add esp,4`. | cdecl, um argumento de 4 bytes; EAX é memória ou zero. Pool do motor, não CRT. O alocador ignora tamanho; o caller da tarefa pede `0x28`. |
| Pool | `0x61A500`: `[ecx+8]` tamanho, `[ecx+4]` bitmap; `0x61A527`: teste bit `0x80`; `0x61A55A`: `C1 E0 07` (`index<<7`), soma `[ecx]`. Delete `0x61A5B0`: `sar eax,7`, marca bit `0x80`, `ret`. | Objetos `+0`, bitmap `+4`, tamanho `+8`, cursor `+0x0C`; stride `0x80`, geração nos sete bits baixos. Delete cdecl com ponteiro de 4 bytes, cleanup do caller. |
| Construtor `0x668120` | `8B F1` preserva ECX; após quatro pushes usa `[esp+0x14/0x18/0x1C/0x20/0x24/0x28]`. `0x668152`: `C7 06 A8 FD 86 00` (vtable). EAX=ESI; `0x668221`: `C2 18 00`. | thiscall; seis slots de 4 bytes: int, ponteiro para vetor XYZ de 12 bytes, float, float, bool, bool. Bools usam byte baixo mas ocupam slots de 4 bytes. Retorna objeto e remove 24 bytes. Caller nativo `0x66CEEA–0x66CEFE` confirma seis pushes; não existe argumento tracking extra nesta ABI. |
| Vtable `0x86FDA8` | 11 DWORDs: `66DB80,66CEA0,421190,4211A0,668230,421180,4211B0,61A430,66DBA0,66DC40,668570`. GetType `0x668230`: `B8 87 03 00 00 C3`. | Identidade do construtor e métodos observados no Hoodlum; id `0x387`. Campos escritos até `+0x24`, alocação nativa `0x28`. Não se instancia uma classe C++ reconstruída na ASI. |
| Intelligence / manager | `0x607168`: `89 2E` (dono em `+0`); `0x607161`: `8D 4E 04`; `0x6816A6`: `89 42 2C` (dono do manager). Caller `0x41C1AD` lê `[ped+0x47C]`; `0x41C1B8` soma 4. | Manager é `intelligence+4`; seu dono é `manager+0x2C`. Runtime exige ambos os donos iguais ao PED autenticado, que não pode ser CJ. |
| Slot / `SetTask 0x681AF0` | Caller `0x41BE99`: `6A 03`; `0x41BE9C`: `83 C1 04`; chamada. `0x681B1C`: `89 1C BE` (guarda raiz no slot); chama `0x681A30` e `0x681970`. `0x681B46`: chamada virtual destrutora com argumento 1; zera slot. `0x681B56`: `C2 0C 00`. | thiscall, três slots de 4 bytes: task*, índice, bool; retorna void e remove 12 bytes. Bool não é lido nesse corpo. Substitui/destrói raiz anterior e pode destruir a nova antes de retornar se a árvore não resultar em tarefa simples. A ASI exige slots 0–3 vazios e usa índice 3; ownership foi transferido mesmo quando o slot final não contém o ponteiro. |
| Destruição da tarefa | `0x66DB83` chama `0x668240`; este salta a `0x61A3D0`. `0x61A3F3` lê filho `+8`, chama destrutor virtual com 1 e zera filho. `0x66DB90` chama `0x61A5B0`. Deletor termina `C2 04 00`. | Destrutor deletor thiscall com flags de 4 bytes; destrói subtarefa e libera slot no pool. Manager destrói todas as raízes primárias/secundárias em `0x6816D0`. Detour Hoodlum `0x403AAB`: `C7 04 B7 00 00 00 00` e salto a `0x6816EA` zeram a raiz. |
| F8 e tarefa ativa | `0x610F26` chama `0x563280`; `0x610F38`: `FF 10`, após ECX=PED e `push 1`. Destrutor base PED `0x5E877F` lê intelligence `+0x47C`, chama `0x607300`; em `0x60737A` soma 4 e em `0x607385` chama `0x6816D0`. | Entrada F8 remove do mundo e delega ao destrutor virtual do PED. Há caminho nativo de cleanup intelligence → manager → tarefas/filhos/pool. A ASI não deve destruir/cancelar manualmente tarefa transferida. Isso é evidência estática, não validação física de F8 durante caminhada. |

## Equivalências e limites da identidade

Os nomes `CTaskComplexGoToPointAndStandStill`, `CPedIntelligence`, `CTaskManager`, `PEDMOVE_WALK=4`, `TASK_PRIMARY_PRIMARY=3` e a interpretação dos flags vêm das fontes plugin-sdk/Reversed nas revisões fixas citadas em [movimento](ped-smoke-movement.md). No Hoodlum foram confirmados números, acessos, branches, tamanho da alocação, pilha, vtable e fluxo de destruição; o executável não fornece nomes C++. Caminhada/animação e o efeito visual de parada ainda são equivalências a confirmar no smoke. Nenhuma DLL Compact ou implementação de terceiros foi incorporada.

Antes de atribuir: path/base/SHA e arquivo retido, CALL original/instalado, thread/caller, gameplay/streaming e referência/pool do PED são os gates existentes. Os 15 ranges adicionais de tarefas/vtable foram conferidos no PE: código `.text` RX (`0x60000020`), vtable `.rdata` R (`0x40000040`). Em runtime exigem `MEM_IMAGE`, allocation base `0x400000`, RX/R exatos e bytes iguais ao arquivo admitido. Isso não comprova proteções runtime por leitura do arquivo: os checks live só executarão no futuro smoke.

Task pool: tamanho positivo ≤10.000, ponteiros presentes, bitmap legível e pelo menos oito slots livres antes da alocação; retorno deve estar no intervalo/alinhamento do pool e com bit livre apagado. Construtor deve retornar a mesma memória/vtable. Após `SetTask`, revalida ownership do PED e compara somente o slot do manager; não desreferencia/libera a tarefa. Não há reserva de capacidade futura, identidade por geração da tarefa após transferência, nem verificação transitiva de todo o motor. Recusas continuam sem retry/destrutores especulativos.

## Separação de builds

| Target | Opção CMake | Defines de spawn / movimento | Gates |
| --- | --- | --- | --- |
| `grandmp_ped_smoke` | padrão | ausentes / ausentes | false / false |
| `grandmp_ped_smoke_enabled` | `GRANDMP_BUILD_PED_SMOKE_ENABLED=ON` | 1 / ausente | true / false |
| `grandmp_ped_smoke_move_enabled` | `GRANDMP_BUILD_PED_MOVE_ENABLED=ON`, default OFF | 1 / 1 | true / true |

As opções são independentes. Novo target `EXCLUDE_FROM_ALL`, output `client/windows/ped-smoke-move-enabled/<Config>/GrandMPPedSmoke.asi`; não modifica o padrão ou o antigo. Ambos os defines precisam valer exatamente 1. Variáveis de ambiente sozinhas não alteram esses constantes; opt-in/F6 e identidade continuam obrigatórios quando há gate de spawn aberto.

Executado: Debug/Win32, diretório novo `build/alpha05-followup1-win32`, opção antiga ON e nova OFF por default. Compilados somente padrão, antigo habilitado, seus dois hosts de testes e quatro hosts puros de gates. `ctest -j 1`: **24/24 padrão + 24/24 antigo habilitado + 4/4 matriz (00,01,10,11) = 52/52**. Movimento fechado nas duas ASIs; testes de tarefa usam fakes. Matriz não carrega ASI nem vincula rotinas/endereços nativos; caso 11 não é uma ASI de movimento. Inicialização da ASI padrão bloqueou; antiga recusou identidade do host descartável; callbacks=0 em ambas. Build sem warnings, `git diff --check` limpo.

Configuração separada `build/alpha05-followup1-move-plan-win32`: nova opção ON e antiga OFF, apenas para inspecionar o projeto gerado (defines, output e exclusão de ALL_BUILD). **Nenhuma compilação do target de movimento**, nem `.asi`/`.obj` dele. `build/alpha05-followup1-review/build.txt`, `ctest.txt`, `artifacts.json`, `preservation.json` e `followup1-incremental.patch` são o handoff local. O manifesto compara SHA-256 e timestamp dos 53 ASIs preexistentes (inclui os instalados por leitura) e preservação do milestone/índice real. Nenhum stage ou alteração de `backups/`.

## Decisão e risco restante

**GO técnico para compilar explicitamente o target de movimento numa etapa posterior, após revisão/autorização.** As chamadas diretas estudadas têm evidência Hoodlum suficiente e a matriz impede habilitação acidental nas builds existentes. Esta decisão não executa a próxima etapa.

**NO-GO para instalação/smoke nesta entrega.** Ainda falta produzir/auditar a futura ASI com ambos os gates, executar seus testes em host descartável e obter autorização do operador. F8 com tarefa ativa permanece um risco físico específico: o controlador para suas consultas e a cadeia de destruição nativa existe, mas animações, referências/eventos internos e cleanup do filho em execução não foram exercitados no Hoodlum. Teste físico futuro separado deve verificar F8 durante/depois da caminhada, ausência de crash/respawn e desaparecimento somente do experimental. Não inserir cleanup manual após `SetTask` como compensação. Timeout encerra observação, não cancela a tarefa do motor.

Nenhuma alteração nesta revisão em `ped_smoke.cpp`, thunk, CALL/hook `0x53C05C`, endereços/ABI, geometria, streaming, PIN, spawn, F6/F7/F8 ou lifecycle. Nenhum GTA executado, ASI instalada/copiada, save alterado ou commit/push/merge.
