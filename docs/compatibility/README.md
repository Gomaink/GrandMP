# SA-MP API Compatibility Registry

Registro de planejamento, **não um include GrandMP compatível nem runtime Pawn**. Em 2026-10-13 não há VM AMX, registro de natives ou gamemode no produto: todos os itens do catálogo são `PENDING`, inclusive os relacionados a posições já lidas pelo observer. Um backend que lê CJ não implementa `GetPlayerPos(playerid, &Float:x, &Float:y, &Float:z)` para scripts.

## Artefatos e proveniência

- [sources.lock.json](sources.lock.json): repositório, revisão/versão, commit completo e SHA-256 de cada arquivo consumido. Fonte de verdade para reprodução.
- [catalog.json](catalog.json): cada declaração com identidade, assinatura, kind, categoria, contexto server/NPC, condições, origem, versão/commit/linha, semântica referenciada, status, dependências e critérios de teste.
- [catalog.md](catalog.md): vista gerada de funções/callbacks públicos e contagens por fonte/categoria.
- [essential-profile.json](essential-profile.json): **perfil proposto** para fechar Alpha 0.8, com nomes extraídos do snapshot SA-MP R2 e verificados por teste. Não é suporte anunciado.
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md): origem/licenças e limites de uso das referências. Não foram incorporadas implementações, includes ou binários de terceiros ao core/servidor/ASI.
- [validation.md](validation.md): comandos/resultados executados, contagens auditadas, revisões e limitações do inventário inicial.

Fontes fixadas:

| Fonte / papel | Revisão | Commit |
|---|---|---|
| SA-MP clássico R2 — mirror dos includes de release | `0.3.7-R2-2-1` | `7b194986946f64e8c0a4d4223aafec704ffd6b88` |
| SA-MP clássico DL — snapshot separado | `0.3.DL-R1` | `c8c0cdc3cb7ccc1c793bd6d38e17e7fcb2b5960f` |
| Includes oficiais open.mp | `master` na captura | `46a5d1d7ac8985e4104f745c462a247a0c2eb6a0` |
| Documentação pública SA-MP/open.mp | `master` na captura | `bf122d1bd9bc02d60aed5e3532798f4040449ae9` |
| Streamer, plugin independente | `master` na captura | `49d48e515b9a095f016fb6a208a2cd94c42acda5` |
| Wiki Streamer, histórico independente | `HEAD` na captura | `52c69e0e62da4846a8b68b75dcc65d0915d17fbe` |

`pawn-lang/samp-stdlib` é publicação comunitária dos cabeçalhos atribuídos ao SA-MP Team; sua origem/versão estão no README e nos cabeçalhos. **Não foi autenticada contra um arquivo original do fornecedor.** Essa limitação é registrada, sem chamar o mirror de distribuição oficial homologada. Os includes open.mp são do projeto oficial e melhoram assinaturas/tags; não substituem silenciosamente a ABI/semântica clássica. A revisão registrada é a versão de origem do item, não necessariamente a primeira versão em que ele surgiu.

## O que as contagens significam

O scanner lexical exclui comentários/strings, preserva protótipos multilinha, macros e guards, enum members, aliases, stocks e variáveis públicas. Retém **todas as variantes condicionais**; não executa o pré-processador nem afirma quantas natives uma configuração compilada disponibilizaria. Enums implícitos não recebem valores calculados por suposição. Macro com parâmetros não é constante nem native; forward não prova emissão de evento.

Um `native Alias(...) = Original;` possui nome de binding original, não um novo serviço do engine. Placeholders de documentação `__open_mp_unused_*`, tags/operadores/guards e helpers internos são identificados; não aparecem como API pública suportada. Declarações de `a_npc.inc` clássico pertencem a uma VM NPC separada; o módulo moderno `omp_npc.inc` oferece gerenciamento **servidor** de NPCs. Não mesclar contextos por nome.

JSON é o inventário completo dos arquivos de include fixados, não só da lista resumida Markdown. Mesmo nome pode ocorrer em R2, DL, open.mp, NPC ou branches diferentes, com assinatura/valor diferente. Portanto, **não somar linhas como número de funcionalidades suportadas**, não deduplicar por nome global e não declarar “API completa”. Os arquivos de runtime Pawn incluídos pelo open.mp são rotulados `PAWN_STDLIB`, separados das extensões multiplayer.

`OPENMP_VARIANT_OF_SAMP` identifica nome declarado no mesmo contexto clássico, mesmo que uma native tenha virado stock, macro ou forward obsoleto. É ancestralidade de superfície, **não equivalência de assinatura, ABI ou comportamento**; `kind`, origem e condições continuam separados. Mudança de kind não transforma uma operação clássica em extensão nova.

As descrições de páginas públicas/comentários são resumos de referência, com `review_state=UNREVIEWED`. `returns_excerpt` não é especificação normativa do GrandMP; páginas open.mp podem documentar correções que não existiam no SA-MP clássico. Analogia entre native dinâmica Streamer e operação SA-MP é explicitamente rotulada, não contrato equivalente. Onde a referência não explica o comportamento (por exemplo callbacks NPC legados/algumas APIs recentes), o item mantém `basis=declaration_only`: existência/assinatura auditadas, semântica comportamental ainda não fechada. **Essa lacuna impede promoção de compatibilidade**, e não será preenchida inventando retorno/efeito. Dependências são um plano inicial por capacidade, refinado obrigatoriamente antes de implementar o item.

Pragmas adjacentes de depreciação ficam em `deprecation_notices`. Stocks possuem alerta de divergência entre resumo documental e corpo real. Exemplo observado: `GetPlayerPoolSize`, `GetVehiclePoolSize` e `GetActorPoolSize` em `_open_mp.inc` estão marcados como broken e retornam `MAX_* - 1` nessa revisão, apesar do resumo histórico descrever maior ID em uso. Sua semântica normativa precisa ser escolhida por perfil; o catálogo não homologa nenhum desses comportamentos.

Cobertura inicial de plugins é **somente a superfície declarada de Streamer**. MySQL, sscanf, YSF, FCNPC, outras bibliotecas/includes e plugins open.mp não são considerados cobertos ou binariamente compatíveis. Docs da wiki Streamer podem mencionar dependências externas; esse detalhe precisa de revisão por item. Alguns callbacks documentados/convenções dinâmicas não têm `forward` no corpus de includes; não inventar um protótipo a partir de exemplo de gamemode. Adicionar fonte/contrato autenticados em atualização própria do registro.

Categorias presentes: Players, Vehicles, Actors, Objects, Pickups, Checkpoints, TextDraws, Dialogs, GangZones, Server, Chat, World, NPC e, conforme as fontes, Classes, TextLabels, Menus, MapIcons, Timers, Variables, Database, HTTP, Network, Animations, Camera, Audio, Streaming, Files, Strings, Math, Time, Console, PawnRuntime e Toolchain. A classificação é metadado de planejamento, não partição infalível de dependências.

## Quatro eixos independentes de compatibilidade

| Eixo | O que exige | O que não demonstra | Estado atual |
|---|---|---|---|
| Código-fonte Pawn | Includes/assinaturas, tags, constantes, macros, stocks, defaults e compiler profile aceitando código de referência; comportamento das chamadas | Compilar não garante native implementada ou todos os valores de retorno | PENDING |
| AMX | Versão/header/cell size, VM/scheduler, layout de strings/arrays/floats, nomes/bindings e execução efetiva do conjunto usado pelo AMX | AMX válido não possui automaticamente todas as natives; não habilita plugin binário | PENDING |
| Protocolo SA-MP | Handshake, wire/transport/eventos e cliente externo realmente interoperando em versões declaradas | API Pawn similar não implica wire similar. UDP GrandMP v1 é próprio | Não implementado; fora do objetivo 1.0 proposto |
| Plugins | ABI/exports/arquitetura/OS/memória/VM/hook e lifecycle de cada plugin, com execução real | Implementar nomes Streamer não permite carregar sua DLL/SO nem componente C++ open.mp | PENDING por plugin; sem promessa universal |

O mesmo item pode ser VERIFIED para um perfil de fonte/AMX no servidor Windows e PENDING no Android ou em outro compiler. Eixos wire/plugin não são promovidos em função do estado da API. Não assumir ABI binária x86 de plugin em servidor x64/Linux. Player ID Pawn exige mapa estável para sessões GrandMP uint64/generações; não converter sessão inteira para cell nem transmitir ponteiros. Constantes de limite clássico são dados do perfil compilado, não prova de capacidade simultânea do servidor atual (64 sessões).

## Estados e evidência obrigatória

| Estado | Definição | Evidência mínima para promover |
|---|---|---|
| PENDING | Não existe comportamento de API no GrandMP. Assinatura extraída, interface, stub ou simples compilação continuam aqui | Contrato individual de versão/contexto e decisão de escopo antes de implementação |
| PARTIAL | Subconjunto útil realmente produz efeitos; faltam partes explicitamente descritas do contrato | Código/binding, demonstração dos efeitos que existem, limites e casos não implementados publicados |
| IMPLEMENTED | Contrato do perfil inteiro foi implementado, incluindo efeitos e erros; falta execução completa da suíte | Revisão independente da implementação real e rastreio de todos os requisitos; stub não qualifica |
| TESTED | Suíte automatizada pertinente executada, com assinaturas/retornos/erros/ordem e resultados guardados | Build/compiler/revisões, comandos, corpus e resultados reais; sem apenas testes que imitam o código |
| VERIFIED | TESTED + integração end-to-end e smoke físico quando a capacidade depende do GTA | Evidência por plataforma/GTA/loader/build/contexto/data; efeitos no produto, teardown e regression |

É proibido pular para IMPLEMENTED porque a native foi registrada, retornar sempre sucesso ou simular efeito com log. Um stock cujo native transitivo é stub também não qualifica. Uma chamada manual a `public OnPlayerSpawn` não prova que o evento é emitido no momento correto. Para natives puramente servidor (timer, console, strings, consultas a estado autoritativo), integração AMX/servidor com efeitos reais pode ser VERIFIED sem iniciar GTA artificialmente.

Promoções futuras vivem em manifesto de claims **separado** do catálogo gerado, chaveadas por `id`, commit/assinatura/contexto/perfil e plataforma, com implementação/testes/revisão/evidência. Introduzir esse manifesto quando houver primeiro binding real; não editar status à mão no arquivo que o gerador recria. Mudança de fonte, compiler, VM, backend ou versão GTA exige revalidação; não herdar VERIFIED automaticamente. Hábitos históricos/bibliotecas populares não substituem referência e teste.

## Plano de testes por item

1. **Contrato:** revisar assinatura/semântica/retorno/efeito/erro/limites do snapshot, diferenças R2/DL/open.mp e emissão/ordem de callbacks. Fixar compiler/VM/cell ABI licenciados. Registrar defaults, tags, ownership e capacidade envolvida.
2. **Fonte:** fixtures positivos/negativos de aridade, tags, arrays/referências, tamanho de string, varargs e resolução de macro/stock/alias; comparar pré-processamento/compilação. O scanner atual é auditoria de declarações, não substitui esse teste.
3. **AMX:** carregar programa real e conferir native binding, retorno, valores escritos por referência e efeitos. Rejeitar versão/header/truncamento/binding ausente; budget e teardown em erro.
4. **Comportamento:** IDs válidos/inválidos/desconectados, handles reaproveitados, valores extremos/NaN, limites, repetição e sequência. Particularidades por item: consulta sem jogador, criação parcial, cancelamento e mudanças de sessão.
5. **Callbacks:** fonte real do evento, argumentos e traço de ordem. Exemplo a confirmar contra contrato escolhido: connect/class/request-spawn/spawn/update, depois death/respawn/disconnect; nem todo evento acontece em toda sessão. Retorno 0/1 propaga ou bloqueia conforme o callback, não uma regra única. Separar filterscript/gamemode e timer/reentrância/shutdown.
6. **Integração:** comparar efeitos servidor-cliente em smoke autorizado quando for mundo/UI/entidade; testar reconexão/loading/encerramento. Testes diferenciais contra servidor de referência legal podem apoiar contrato, sem redistribuir executáveis. Registrar divergências aceitas no perfil antes da homologação.

Critérios específicos iniciais ficam em cada `test_criteria` do JSON; não são testes já executados da API. Releases 0.7/0.8 terão perfis finitos, com cada item VERIFIED ou excluído por revisão explícita. Perfis adicionais não ampliam suporte silenciosamente.

## Reprodução do inventário

Python **3.10+**, biblioteca padrão; Git apenas para buscar a wiki quando faltar cache. Não executar código de fontes baixadas. Em PowerShell, a partir da raiz:

```powershell
python scripts/compatibility/generate_registry.py --fetch
python scripts/compatibility/generate_registry.py --check
python -m unittest discover -s scripts/compatibility -p 'test_*.py' -v
```

`--fetch` baixa texto somente em `build/api-registry-sources/` e verifica **todos** os hashes do lock. Wiki usa clone bare/git-show na revisão fixada, sem carregar código de plugin. Sem `--fetch`, cache ausente/modificado falha; não recorrer a `master`, aceitar hash divergente ou trocar commit automaticamente. `--check` regenera em memória e compara JSON/Markdown determinísticos, sem editar produto. Fonte e Markdown de documentação são referências em cache ignorado, não dependências CMake. A reprodução sem rede exige cache já preenchido.

Para atualizar fontes: tarefa própria, revisão das licenças/proveniência, alterar commit/hashes, gerar inventário novo e revisar diferenças de assinatura/constantes/condições/contextos. Exigir revisão architect/reviewer. Revalidar perfil essencial e claims; o gerador não atribui novo suporte.
