# Alpha 0.2.8 — avaliação do hook de atualização Hoodlum

## Estado e baseline Git

Nenhum hook foi instalado no GTA ou conectado ao `GrandMP.asi`. O repositório permanece sem commit. O inventário auditado antes das edições desta etapa contém **57 arquivos não rastreados**: `.codex/` (7), `agent-definitions/` (6), `client/` (14), `docs/` (8), `shared/` (7), `tests/` (10), além de `AGENTS.md`, `.gitignore`, `CMakeLists.txt`, `README.md` e `server/README.md`. A contagem preliminar de 56 estava incorreta. `build/` está ignorado. A inspeção por nomes e padrões não encontrou binários, assets proprietários do GTA, arquivos `.env` ou indícios de chaves no conjunto elegível. A proposta, **sem executá-la**, é um commit inicial `chore(repo): establish GrandMP Alpha 0.2.7 baseline` somente com esses 57 arquivos; o protótipo e este documento ficariam em revisão posterior.

## Ponto candidato, com limite de evidência

O [plugin-sdk em `15f15b60bbf74c106e1b496ff92c98764abf4605`](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/meta/meta.CGame.h) registra `CGame::Process` em `0x53BEE0`, assinatura `void()`/`CallingConventions::Cdecl`, e uma referência `CALL` em `0x53E981`. **A referência é rotulada `GAME_10US_COMPACT`**; não autoriza transferir ABI ou bytes ao Hoodlum. O [evento `gameProcessEvent` da mesma revisão](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/shared/Events.h) intercepta o CALL interno, com os problemas de propriedade/restauração descritos em [hoodlum-player-position.md](hoodlum-player-position.md).

No executável local de SHA-256 Hoodlum `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`, a desassemblagem MSVC x86 confirmou `0x53BEE0: 83 EC 0C 53 56 57` (`sub esp,0Ch; push ebx; push esi; push edi`), quatro instruções completas em seis bytes; e `0x53E981: E8 5A D5 FF FF`, CALL para `0x53BEE0`. O epílogo observado restaura registradores/pilha e termina em salto de cauda para `0x6EB710`, não em `ret` simples. Isso torna o endereço uma **candidata de entrada** para estudar um patch JMP de cinco bytes, sem reescrever o CALL interno. No MinHook v1.3.4, o trampoline cobriria as três primeiras instruções (cinco bytes) e saltaria para `0x53BEE5`, onde executaria o `push edi` original; essa previsão estrutural não valida a instalação no jogo. Ainda faltam validação completa da ABI Hoodlum em todos os chamadores, fase da execução e estado do jogador após a rotina original. Não existe assinatura de detour aprovada para o GTA.

## MinHook v1.3.4 e protótipo descartável

O [MinHook v1.3.4, commit `c3fcafdc10146beb5919319d0683e44e3c30d537`](https://github.com/TsudaKageyu/minhook/tree/c3fcafdc10146beb5919319d0683e44e3c30d537) é C para Windows x86/x64, sem Boost, sob [BSD de duas cláusulas com avisos HDE32/HDE64](https://github.com/TsudaKageyu/minhook/blob/v1.3.4/LICENSE.txt). Nenhum fonte MinHook foi adicionado ao projeto: o checkout usado fica em `build/alpha028-minhook-src`, ignorado. O CMake da tag ainda escreve versão 1.3.3; a verificação do protótipo exige o commit exato e uma árvore limpa.

O ensaio em `tests/minhook-isolated/` compila separadamente com MSVC 19.51 x86. Ele carrega duas DLLs próprias: uma exporta `int __cdecl ProbeTarget(int)`, a outra contém o detour. `MH_CreateHook` entrega trampoline, e o detour chama a original antes de somar 100. O teste confirmou retorno 12 sem hook, 112 com hook, quatro threads e 4.000 chamadas concorrentes enquanto habilitado, instalação duplicada recusada, alvo nulo recusado, desativação/remoção, descarregamento do detour e 1.000 chamadas posteriores sem callback. **Não** executou habilitação ou remoção com threads chamando a função; os workers terminam antes disso. Logo o teste não prova instalação concorrente segura.

Reprodução, sem GTA:

```powershell
git clone --depth 1 --branch v1.3.4 https://github.com/TsudaKageyu/minhook.git build\alpha028-minhook-src
cmake -S tests\minhook-isolated -B build\alpha028-minhook-probe -G "Visual Studio 18 2026" -A Win32 -DMINHOOK_SOURCE_DIR="D:/Programacao/Workspace/Applications/grandmp/build/alpha028-minhook-src"
cmake --build build\alpha028-minhook-probe --config Debug
ctest --test-dir build\alpha028-minhook-probe -C Debug --output-on-failure
```

## Bloqueios para o GTA

No [código de `Freeze` do MinHook](https://github.com/TsudaKageyu/minhook/blob/v1.3.4/src/hook.c), uma falha individual de `OpenThread`/`SuspendThread` zera o ID dessa thread e pode deixar a ativação retornar `MH_OK`. `ProcessThreadIPs` também não propaga certas falhas de contexto. `EnableHookLL` escreve JMP usando `VirtualProtect` e `FlushInstructionCache`, sem revalidar imediatamente antes os bytes que outro plugin pode ter alterado. A enumeração/suspensão não comprova ausência de uma thread criada depois. Assim, a v1.3.4 sem mitigação revisada **não fornece a falha conservadora nem a quiescência exigidas para o GTA**. MinHook trabalha no prólogo de função; não transforma automaticamente o CALL `0x53E981` em ponto seguro.

O ASI atual cria `WindowsClientRuntime` temporário dentro de `InitializeASI`; o objeto termina quando a exportação retorna. O PIN testado na Alpha 0.2.5 não está conectado à produção, e o contexto fora do loader lock ainda não está comprovado por observação direta. O `HookManager` sintético contém `std::terminate` para registro ativo e não deve integrar o ASI armado. Não existe política comprovada para manter callback, estado, código e trampoline válidos até o encerramento, ou para conviver com patches de terceiros. Não se deve restaurar instruções em `DllMain`.

**Veredito técnico: NO-GO para integrar ou executar um hook no GTA neste slice.** A próxima avaliação precisa resolver suspensão/concorrência com falha conservadora, propriedade dos bytes, residência do módulo e dados, entrada fora do loader lock e a ABI/fase da função Hoodlum. O protótipo não muda o diagnóstico ASI, o core, Android ou servidor. Nenhuma dependência MinHook é vinculada ao produto.
