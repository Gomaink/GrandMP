# Alpha 0.4, Slice 0.1 — fase após população

Incremento sobre a implementação não commitada do Slice 0, HEAD `eee2377`. Um agente, sem subagentes. A ASI deixa de interceptar ProcessControl: streaming, criação e remoção passam à entrada de uma fase de CGame::Process fora da iteração de CWorld. **Instalação física permanece desabilitada pelo literal `physicalExecutionApproved=false`. Nenhuma variável/export o sobrepõe.**

## Ponto escolhido e prova local

Executável lido, sem iniciar processo: `D:/Jogos/GrandMP-Smoke/gta_sa.exe`, 14.383.616 bytes, base `0x400000`, SHA-256 `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`. MSVC dumpbin x86 14.51.36231. O fluxo encontrado no arquivo Hoodlum é:

```text
0053BF0B E8 60 27 ED FF   call 0040E670  CStreaming::Update
0053BFC7 E8 34 E0 F2 FF   call 0046A000  CTheScripts::Process
...
0053C02D 53               push ebx      ebx=1
0053C030 E8 1B A6 0D 00   call 00616650  CPopulation::Update(true)
0053C035 83 C4 04         add esp,4
...                                    medição de tempo
0053C050 EB 0A            jmp 0053C05C
0053C052 6A 00            push 0
0053C054 E8 F7 A5 0D 00   call 00616650  CPopulation::Update(false)
0053C059 83 C4 04         add esp,4
0053C05C E8 FF E2 1F 00   call 0073A360  CWeapon::UpdateWeapons
...
0053C095 E8 06 C4 02 00   call 005684A0  CWorld::Process
```

Os dois caminhos retornam de população e convergem em **`0x53C05C`**. A ASI executa seu corpo **antes** de encaminhar UpdateWeapons: é a primeira chamada no ponto de convergência, sem iterador de World aberto nesse fluxo. [CGame::Process, gta-reversed c65cfe85](https://github.com/gta-reversed/gta-reversed/blob/c65cfe85f90bfe80cdb3b82701891f376a3482ea/source/game_sa/Game.cpp) confirma a ordem lógica; o binário local confirma os desvios e destinos. A DLL Compact desse projeto não é utilizada.

[plugin-sdk CWeapon.cpp, 15f15b60](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/CWeapon.cpp) identifica `0x73A360` como `void __cdecl()` sem argumentos. O corpo Hoodlum contém três CALLs e um JMP final (0x73A36F→0x7360D0); não há argumento/this a reconstruir. O thunk salva flags/GPRs e FXSAVE em 16 bytes de alinhamento, inicializa um ambiente FP local e restaura x87/MMX/SSE/MXCSR. Seu JMP final para o original imutável mantém o retorno nativo `0x53C061`, sem criar outro frame de retorno. FXSR/SSE2 são gates de instalação. O CALL `0x53E981` permanece intocado; não é registrado evento plugin-sdk.

### Thread e publicação

[WinMain.cpp](https://github.com/gta-reversed/gta-reversed/blob/c65cfe85f90bfe80cdb3b82701891f376a3482ea/source/app/platform/win/WinMain.cpp), [app.cpp](https://github.com/gta-reversed/gta-reversed/blob/c65cfe85f90bfe80cdb3b82701891f376a3482ea/source/app/app.cpp) e [app_game.cpp](https://github.com/gta-reversed/gta-reversed/blob/c65cfe85f90bfe80cdb3b82701891f376a3482ea/source/app/app_game.cpp) fundamentam o caminho síncrono janela→loop→rsIDLE→Idle→CGame::Process. No Hoodlum: WinMain chama 0x745560 em 0x7487A8; esse código chama CreateWindowExA pelo IAT 0x8582E8. Em 0x7487AD..B3 armazena o HWND em `[[0xC17054]]`. O mesmo loop chama PeekMessageA/IAT 0x8582BC em 0x748A57 e RsEventHandler em 0x748D9B. Em 0x619B6C chega a AppEventHandler; o stub Hoodlum 0x4018CE remapeia rsIDLE (0x1A) ao slot 7, destino 0x53ECB8, que chama Idle em 0x53ECBD. Não se escolhe arbitrariamente uma janela do PID.

O worker apenas prepara. Depois de opt-in/F6, com PIN já adquirido, um [WH_GETMESSAGE específico da thread](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexw) publica os cinco bytes **na própria thread da janela**, enquanto ela processa mensagens; o callback de instalação não faz engine calls. Isso evita escrever um CALL de cinco bytes concorrentemente com a thread executora. Não há escrita pela thread de preparação. Timeout cancela apenas instalação ainda não iniciada; se já começou, aguarda seu término antes de liberar armazenamento. O worker retira o bootstrap antes de abrir o dispatch (`preparation=3`). Falha mantém dispatch fechado e módulo pinado. [UnhookWindowsHookEx pode deixar callback em andamento](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-unhookwindowshookex); o handshake/PIN mantêm seus dados/código válidos. Não há hot-unload ou restauração automática de bytes desconhecidos.

Antes de publicar, compara CALL original, destinos/contexto de CGame, UpdateWeapons, Population, criação da janela, loop e dispatcher com o arquivo admitido e páginas RX. Depois confere cache, proteção e bytes publicados. No callback, exige TID exato e retorno `0x53C061`, protege contra reentrância e revalida código incluindo seu próprio CALL instalado. Os gates anteriores de executável/caminhos, CJ, pool, referência e identidade permanecem.

## Streaming e falha depois de AddPed

Streaming/spawn/remoção só entram nessa fase quando há gameplay admitido, streaming inicializado e `ms_disableStreaming`, `m_bLoadingScene`, `ms_bLoadingBigModel` todos falsos. Endereços desses três bools vêm de [CStreaming.cpp, plugin-sdk 15f15b60](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/CStreaming.cpp): 0x9654B0, 0x9654BD, 0x8E4A58. CStreaming::Update já retornou nessa iteração. Mantém RequestModel/LoadAllRequestedModels uma vez, consulta de estado loaded e rechecagem antes de AddPed; não há streaming bloqueante em ProcessControl nem no bootstrap.

O retorno bruto de AddPed fica retido separadamente da referência que o motor pode limpar. Null é falha terminal. Identidade não comprovada registra `addped_returned_identity_unknown_retained_no_delete_no_retry`, conserva ponteiro/referência para diagnóstico e bloqueia novo spawn; não chama remoção presumida. Identidade comprovada com render object inválido entra em `cleanup_pending`, mantendo ownership até a próxima fase segura. Ali revalida identidade e remove somente seu PED; perda de identidade ou falha de remoção conserva o token e encerra em failed. Nenhum desses caminhos autoriza segunda criação. Um código nativo que falhe por SEH/crash continua fora da garantia do controlador.

## Validação e handoff

Build exclusivo dos targets novos, Debug|Win32; não reconstruídos os ASIs anteriores:

```powershell
cmake --build build/alpha036-win32 --config Debug --target grandmp_ped_smoke grandmp_ped_smoke_tests -- /p:BuildProjectReferences=false
ctest --test-dir build/alpha036-win32 -C Debug -R '^ped_smoke_' --output-on-failure --output-log D:/Programacao/Workspace/Applications/grandmp/build/alpha0401-win32/ped-ctest.txt -j 1
git diff --check
```

Os nove testes anteriores são preservados. Novos `post_create` (cleanup adiado/identidade desconhecida/perdida/falha de remoção) e `phase_abi` (mesmo thunk, original descartável, ordem, retorno/pilha, GPRs/flags/x87/MXCSR/oito XMM e encoding rel32). O teste de inicialização carrega a ASI real em host descartável e confirma gate fechado mesmo com opt-in. Testes de estado usam engine fake; a ABI do thunk é executada nativamente x86. Logs/build/evidência/patch incremental ficam em `build/alpha0401-win32/`. A evidência completa do ponto é `hoodlum-phase-evidence.txt`, com assert de destinos/branches/imports sobre o SHA aprovado; `lab-disasm.txt` contém o thunk compilado.

Resultado final: **build Debug|Win32 sem warnings; 11/11 testes aprovados em 0,31 s; `git diff --check` e verificação reversa do patch incremental aprovados.** ASI fechada em `build/alpha036-win32/client/windows/Debug/GrandMPPedSmoke.asi`, SHA-256 `0836D3B8F87D0BD35691B40B21C5C54EC7DD3BCEBD569C254AB2EC50602B1784`. Hashes e timestamps dos dois ASIs anteriores conferem com a entrada da tarefa; registrados em `artifacts.json`. O patch incremental usa como base os arquivos recebidos do Slice 0, incluindo os ainda untracked; não confundir com o diff total contra HEAD.

**GO para revisão; NO-GO para instalação/smoke enquanto o literal estiver fechado.** A prova estática resolve a posição dentro de CWorld do slice anterior. Não substitui a observação da thread/estruturas/streaming vivos, do bootstrap no jogo ou da visibilidade e lifecycle do PED. Não foram executados GTA, debugger, hook/patch físico ou cópia para scripts. GrandMP.asi e GrandMPNativeCounterExperimental.asi preservados; sem commit/push.

Após revisão e autorização de uma nova build habilitada, usar o [procedimento isolado anterior](ped-smoke-alpha040.md#evidência-faltante-e-smoke-somente-após-aprovação), considerando os bloqueios/fluxo atualizados aqui. Após F6 exigir `post_population_phase_installed_original_immutable_module_pinned`, `callback_executed`, `model_loaded`, exatamente um `ped_created`; CJ e o segundo PED simultâneos, jogo responsivo, sem repetição. F8 deve remover somente esse PED e não gerar outro. `cleanup_pending`/failed reprova o smoke; preservar log e encerrar a sessão, sem tentar trocar hooks ou remover entidade desconhecida. Instalação/remoção de arquivo só com o processo encerrado; não salvar a sessão.
