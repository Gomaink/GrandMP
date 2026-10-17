# Alpha 0.4, Slice 0 — um PED local

> Registro histórico do Slice 0. O callback, o build e os resultados atuais estão em [Slice 0.1](ped-smoke-alpha0401.md); o hook de ProcessControl descrito abaixo foi substituído.

Base limpa: `eee23777986baebe8849bcd2090b6f298aef6f3e`, master. Um agente; sem subagentes. Implementação exclusiva de Windows x86 em `client/windows/tools/ped_smoke*`, testes em `tests/ped_smoke_tests.cpp`. Sem alterações no core, rede, ASIs anteriores, câmera, controles ou saves.

## Entrega e gate

Target `grandmp_ped_smoke`, saída `build/alpha036-win32/client/windows/Debug/GrandMPPedSmoke.asi`. O cache Win32 existente foi reutilizado para não reconstruir dependências/ASIs. O módulo contém o fluxo nativo de streaming/criação/remoção e um instalador candidato de slot de vtable. **`physicalExecutionApproved=false` é literal; nenhum opt-in/export o sobrepõe. A build entregue não instala hook nem cria PED.**

O bloqueio está antes da admissão/instalação: no carregamento desta build, nem sequer se consultam estruturas do GTA. `DllMain` somente guarda HMODULE. `InitializeASI` agenda uma única preparação com referência própria ao módulo; essa thread apenas verifica/prepara e nunca executa operações de engine. O worker termina liberando sua referência. Não existe thread que simule frames.

**GO para revisão do código/laboratório. NO-GO para smoke de PED visível.** Não basta aprovar a cópia do arquivo ou trocar o literal: faltam as evidências de execução/iteração indicadas abaixo. O sucesso físico solicitado não foi alcançado nem declarado.

## Referências estudadas e limites

- [gta-reversed, Population.cpp, c65cfe85](https://github.com/gta-reversed/gta-reversed/blob/c65cfe85f90bfe80cdb3b82701891f376a3482ea/source/game_sa/Population.cpp): para civil, AddPed seleciona CCivilianPed, posiciona/orienta e insere no World. `makeWander=false` retorna antes de atribuir tarefas de wander; não exige uma segunda inserção. RemovePed remove do World e usa destrutor virtual. Nenhuma implementação foi copiada. A licença não foi localizada em LICENSE/LICENSE.md; o README exige Compact. Somente referência de lógica, sem DLL, layouts Compact ou código incorporado.
- [plugin-sdk, 15f15b60](https://github.com/DK22Pac/plugin-sdk/tree/15f15b60bbf74c106e1b496ff92c98764abf4605): CPopulation, CStreaming, CPed, CEntity/CReference, CPools, CWorld, CTimer, Events e EventList. Licença [zlib](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/LICENSE), autores Dmitry K., fastman92, LINK/2012 e comunidade GTA. Usados fatos de ABI/endereço/layout, não fontes do SDK nem seu registro de eventos. `gameProcessEvent` usa o CALL proibido; EventList instala no registro, portanto não há registro de evento SDK na preparação.
- CubicMP local: `D:/Programacao/Workspace/Applications/CubicMP`, ref main `5476e9b9db22efd20f451de8075908f2a5dfdf2e`, licença MIT, copyright 2026 Cubic Multiplayer. Lidos GameTickHook.cpp, Ped.cpp, main.cpp. Aproveitada a sequência de streaming/one-shot; implementação nova. O original é publicado depois do patch e há criação de thread em DllMain: ambos rejeitados aqui. O slot histórico também diverge do binário atual, conforme abaixo.
- [reVC, a26fff4a](https://github.com/mrxenginner/reVC/tree/a26fff4af555a8a425ac9f17d1dd718d00d1f942): Population/Ped/World consultados somente como referência secundária de fábrica/inserção/remoção. [Licença customizada MIT-PoU](https://github.com/mrxenginner/reVC/blob/a26fff4af555a8a425ac9f17d1dd718d00d1f942/LICENSE.md), não tratada como MIT padrão. Nenhum código, offset, layout, ABI ou componente de Vice City foi incorporado. Nenhuma contribuição externa/PR foi feita.

Referências baixadas apenas em `build/alpha040-win32/research/`, ignorado pelo Git. Não foram incluídos arquivos proprietários do GTA ou bibliotecas de terceiros no diff.

## Evidência estática específica do Hoodlum

Somente leitura de `D:/Jogos/GrandMP-Smoke/gta_sa.exe`. SHA-256 conferido: `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`, 14.383.616 bytes, base 0x400000. Desassemblagem via MSVC dumpbin 14.51.36231 x86; fontes fixadas acima. Nenhum processo do jogo foi iniciado ou inspecionado.

| Contrato | Evidência do arquivo local e fontes SA |
| --- | --- |
| Execução candidata | Vtable CPlayerPed em 0x86D168, **índice 10 / offset 0x28 / entrada 0x86D190 = 0x60EA90**. Próximo índice, 11 / 0x2C / 0x86D194, contém **0x54DFB0 (ProcessCollision)**. O slot do CubicMP não é ProcessControl neste arquivo. |
| ProcessControl | 0x60EA90 usa ECX como this, salva ESI e acessa player data em this+0x480; sem argumentos explícitos. Fonte PlayerPed.cpp confirma função virtual void. Thunk x86 `__fastcall(void*,void*)` encaminha via `void(__thiscall*)(void*)` uma única vez, antes do experimento. Não é um detour de instruções. |
| AddPed | 0x612710 contém NOP/JMP para 0x4018C3, que recompõe início de SEH e retorna a 0x612716. Chamador em 0x594D48 empilha quatro argumentos e limpa 0x10 bytes; retorno em EAX. O corpo copia três floats da posição, orienta, chama World::Add em 0x612A10 e salta para o retorno quando o booleano é falso. Ret simples em 0x612C79. ABI __cdecl consistente com SDK; CVector por referência, 12 bytes/alinhamento 4. |
| Streaming | RequestModel 0x4087E0, dois argumentos __cdecl. Desvio em 0x4087F4 para 0x404C95 lê estado em 0x8E4CD0 e retorna a 0x4087F9. LoadAllRequestedModels 0x40EA10 salta para 0x40A45E e volta a 0x40EA15. Operando em 0x408B25 contém tabela 0x8E4CC0; stride 0x14, estado +0x10, loaded=1, conforme CStreamingInfo. |
| Solo/colisão | FindGroundZFor3DCoord 0x5696C0: __cdecl com floats x/y/z, bool* e entidade**, float em ST0; FLD seguido de ret em 0x569741. TestSphereAgainstWorld 0x569E20: CVector **por valor**, raio, entidade ignorada e seis bools; assinatura do CWorld SDK, entrada local presente. |
| Remoção/identidade | RemovePed 0x610F20 chama World::Remove 0x563280, depois destrutor virtual com flag 1; retorna com ret simples. GetPedRef 0x54FF60/GetPed 0x54FF90 usam pool 0xB74490; retorno por EAX. RegisterReference 0x571B70 e CleanUpOldReference 0x571A00 usam this em ECX. |

Offsets SA usados, com fonte: CEntity.h (render object +0x18, flags +0x1C/bIsVisible=0x80, modelo +0x22, referências +0x24, área +0x2F); CPed.h (intelligence +0x47C, player data +0x480, saúde +0x540, tipo +0x598); CReference.h (next +0, endereço referenciado +4). Posição: matrix +0x14, posição da matriz +0x30, eixo direito +0, já utilizados/estudados nos leitores SA do projeto. CTimer.cpp fundamenta pausas 0xB7CB48/49; CStreaming.cpp fundamenta inicializado 0x9654B8. Tipos: CIVMALE=4, MALE01=7.

Arquivo de análise: `build/alpha040-win32/research/hoodlum-profile.txt`, incluindo valores dos 26 slots e hashes das janelas de entrada; desassemblagem completa fica somente em build. A comparação em runtime usa bytes do arquivo admitido, não assinatura Compact. Não se afirma que janelas de entrada de 32 bytes provem todo o grafo de chamadas ou o timing.

## Comportamento implementado

Após futura liberação revisada, identidade exige caminho exato do EXE e do novo ASI em scripts, fingerprint Hoodlum, base, cabeçalho carregado, operando da tabela e bytes dos pontos usados/desvios conhecidos. O arquivo é mantido aberto GENERIC_READ/FILE_SHARE_READ. Instalação exige opt-in, alvo em primeiro plano e F6; valida gameplay/CJ, slot original em memória e em disco, página de imagem somente leitura, assinaturas e PIN. Só altera aquele DWORD alinhado, por compare-exchange, sem RWX ou escrita no código. O original é imutável e inicializado antes da publicação. O corpo do experimento só abre após publicação e proteção confirmadas; falha não autoriza hot-unload ou reparo automático. O hook residente continua encaminhando o original mesmo após remoção lógica do PED.

O callback valida thread da janela do processo, CJ exato, jogador 0 vivo/exterior, pausas, pool, intelligence e streaming. Uma divergência de thread desabilita o experimento. A associação janela/thread do World ainda precisa de prova física; ela não é presumida pela preparação. Sem gameplay, não há chamadas adicionais de engine.

Estados: waiting → model_requested → model_loaded → ped_created; removed e failed são terminais para spawn. RequestModel(7, DEFAULT) e LoadAllRequestedModels(false) ocorrem uma vez. Não se alteram flags de ownership global de modelos/população. Revalida loaded antes de criar. Posição é 2,5 m no eixo direito horizontal do CJ, com solo encontrado, desnível limitado, centro 1 m acima do solo e esfera livre de colisão de raio 0,35 m. Sem solo válido, falha sem spawn. Não há segunda chamada World::Add, escrita manual de posição, tarefa de wander, arma ou animação multiplayer.

AddPed(4,7,position,false) pode retornar um novo ponteiro; ele é registrado no sistema de referências do motor em armazenamento estável e recebe handle do pool. Ownership exige ponteiro exato, diferente de CJ, referência registrada e resolução do handle igual. A flag de visibilidade/modelo/clump precisa estar coerente antes de ped_created; isso **não prova pixels na tela**. Referência perdida nunca autoriza respawn ou remoção de entidade substituta. F8 em primeiro plano ou export RequestRemoval solicita remoção no callback adequado; valida ownership, remove sua referência e usa RemovePed. Cancelamento antes de spawn também é definitivo. Não se descarrega ASI/hook ao remover o PED.

Log ao lado do módulo: `GrandMPPedSmoke.log`. `loaded` distingue carregamento, `callback_executed` distingue ingresso real, `model_loaded` distingue streaming e `ped_created` significa objeto/clump/ownership verificados. Inclui PID, TID, ponteiro e contador de callbacks. Status exportado: 0 antes da inicialização, 1 preparando, 2 bloqueado, 3 instalado, 4 falha. Nesta build o resultado esperado é 2, callback=0 e `installation_blocked_physical_execution_review_missing`.

## Build e validação

```powershell
cmake --build build/alpha036-win32 --config Debug --target grandmp_ped_smoke grandmp_ped_smoke_tests -- /p:BuildProjectReferences=false
ctest --test-dir build/alpha036-win32 -C Debug -R '^ped_smoke_' --output-on-failure --output-log build/alpha040-win32/ped-final-ctest.txt -j 1
```

Os nove casos cobrem espera por gameplay/modelo e timeout, um único spawn em 10.000 callbacks, reentrância, remoção idempotente, identidade perdida, cancelamento, thread errada, falhas de alocação/streaming/solo/NaN/render object e inicialização concorrente de 16 chamadores na ASI real. O host descartável confirma DllMain inerte e gate fechado mesmo com opt-in. Modelos/PEDs dos testes de estado são fakes; não exercitam o motor nem homologam ABI física. **Build final Debug|Win32 aprovado, sem warnings; 9/9 testes passaram em 1,23 s**, log `build/alpha040-win32/ped-final-ctest.txt`, build em `ped-build.txt`. Nenhum teste de debugger/patch foi necessário neste slice; regressões não relacionadas não foram executadas.

SHA-256 da nova ASI **fechada para instalação**, para revisão: `D3337898A93E2F1F977171FC16B1B8D6C409DDC983CD4C4B08324A111C7E70F6`. Produção local preservada: `1421524621D938C2E06F3FF8E8D48298F8332C6B7982E2AEF6027C9B404EB45B`; contador experimental preservado: `AC0B839D5E524DDAD659588EC9B9894C2B0EDC1A07FEA8EB57DB23E2FDCC846C`. Hashes e timestamps dos dois ASIs anteriores foram comparados antes/depois. Arquivos ASI da instalação não foram abertos nem modificados. `git diff --check` aprovado.

## Evidência faltante e smoke somente após aprovação

Bloqueios precisos: ProcessControl está dentro da iteração das entidades móveis de CWorld::Process. Ainda não foi demonstrado que inserir/remover outro PED nesse callback preserva iteradores/listas, nem que LoadAllRequestedModels nesse ponto não reentra em rotinas incompatíveis. Falta comprovar a associação da thread da janela ao callback no gameplay Hoodlum, layouts vivos, estado default das tarefas com makeWander=false e disponibilidade dos serviços nas transições. A presença do slot/assinatura correta não resolve esses contratos. A revisão deve provar esse ponto ou selecionar e validar outra fase permitida; não se substitui por outro hook presumido e não se usa 0x53E981.

**Procedimento proposto, não executado e inaplicável para gerar PED com o gate atual:**

1. Resolver os bloqueios acima, revisar a abertura do literal e aprovar o SHA-256 da nova build habilitada. A build fechada atual só serve a carregamento/diagnóstico; não trocar o literal sem essas provas. Não usar supervisor do contador, debugger externo ou eventos SDK de registro automático.
2. Com GTA completamente encerrado, na cópia isolada, conferir o SHA-256 de `D:/Jogos/GrandMP-Smoke/gta_sa.exe` e inventário de scripts. Manter GrandMP.asi e GrandMPNativeCounterExperimental.asi intocados; não habilitar o contador. Copiar **somente o novo artefato aprovado** para `D:/Jogos/GrandMP-Smoke/scripts/GrandMPPedSmoke.asi`. Registrar hashes antes/depois dos ASIs existentes. Não copiar arquivo Compact.
3. Em um PowerShell do operador, somente para a futura build aprovada: `$env:GRANDMP_PED_SMOKE_ENABLE='1'`; iniciar manualmente `D:/Jogos/GrandMP-Smoke/gta_sa.exe`. Nenhum comando de lançamento/cópia foi executado nesta tarefa. Carregar gameplay exterior, CJ vivo/a pé, área plana e livre; deixar o processo em primeiro plano. Não salvar durante o experimento.
4. Conferir loaded/waiting e somente então pressionar F6 uma vez. Exigir log de callback, modelo carregado e um ped_created. Visualmente conferir **CJ e um segundo PED simultâneos**, jogo responsivo, nenhum terceiro spawn do experimento ao aguardar/mover por alguns segundos. Ponteiro/log não substitui essa inspeção. Ausência de callback, modelo, solo ou PED é falha; não repetir F6 nem habilitar outro hook.
5. Pressionar F8 e confirmar removed, desaparecimento apenas do PED criado, CJ intacto e ausência de respawn. Encerrar o jogo normalmente. Sem restauração/hot-unload do hook durante gameplay; PIN mantém o callback válido até a saída. Em falha/crash, conservar log e descartar a sessão sem tentar writes/continue automáticos.
6. Só após o processo ter terminado, retirar `scripts/GrandMPPedSmoke.asi` e opcionalmente arquivar seu log; remover o opt-in do shell (`Remove-Item Env:GRANDMP_PED_SMOKE_ENABLE`). Não remover/renomear ASIs enquanto o alvo estiver vivo. Reconfirmar hashes dos ASIs anteriores e que só o experimento foi retirado.

O experimento não grava saves; efeitos de salvar durante a sessão não foram homologados, por isso o procedimento exige não salvar. Não há homologação após mudança de área/morte/loading nem garantia contra crash/estado indeterminado. Chamadas de engine com falha de ABI podem derrubar o alvo; não são convertidas em sucesso por captura de exceções. Limites de referência/código/posição recusam sem tentar outra entidade. Revisão final read-only feita pelo próprio agente, conforme a restrição de um único agente. Sem GTA, cópia para scripts, hook físico, commit, push ou merge.
