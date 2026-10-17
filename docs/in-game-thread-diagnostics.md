# Alpha 0.2.7 — diagnóstico passivo de threads no GTA

## Escopo

`GrandMP.asi` registra PID e TID da chamada concreta de `InitializeASI`, além da versão e do diagnóstico Hoodlum já existentes. `DllMain` continua apenas registrando o handle. Não há timer, callback, PIN, hook, leitura de estruturas do jogo ou criação de thread. O observador `grandmp_window_observer.exe` é um processo separado: enumera processos chamados `gta_sa.exe` e suas janelas de topo, sem abrir o processo para escrita, injetar código ou enviar mensagens.

## Evidência esperada

No `GrandMP.log` ao lado do ASI:

```text
[GrandMP][INFO] GrandMP 0.2.0 platform=Windows x86
[GrandMP][INFO] InitializeASI pid=<PID> tid=<TID>
[GrandMP][INFO] Executavel: bytes=14383616 PE timestamp=... SHA-256=a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26
[GrandMP][INFO] Identidade binaria confirmada: GTA SA 1.0 US Hoodlum
```

O PID e TID são valores decimais da sessão e não constantes. O teste Win32 `asi_entry_once` chama `InitializeASI` em um host desconhecido e verifica que a linha contém os IDs reais da thread e do processo de teste exatamente uma vez. Isso não mede a thread dentro do GTA.

## Observador externo

O utilitário usa [`CreateToolhelp32Snapshot`/`Process32First`/`Process32Next`](https://learn.microsoft.com/en-us/windows/win32/toolhelp/taking-a-snapshot-and-viewing-processes) para encontrar `gta_sa.exe`, [`EnumWindows`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumwindows) para janelas de topo e [`GetWindowThreadProcessId`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowthreadprocessid) para a thread criadora de cada janela. `GetWindow(GW_OWNER)`, visibilidade, estilo `WS_EX_TOOLWINDOW` e tamanho distinguem **candidatas a janela principal** de janelas auxiliares ou ocultas. Essa classificação é uma heurística; múltiplas candidatas ou nenhuma candidata devem ser reportadas, não corrigidas por suposição.

O utilitário mostra `HWND`, PID, TID, classe, título, visibilidade, owner e `match-InitializeASI`. Ele não observa chamadas a `DispatchMessage`; não prova que a thread da janela é a thread de atualização do jogo. O Hoodlum local importa `PeekMessageA`, `TranslateMessage` e `DispatchMessageA`, mas a tabela de imports não revela qual thread os chama. Mesmo com IDs iguais, `FindPlayerPed` e `FindPlayerCoors` continuam proibidos neste slice.

## Smoke físico manual — somente instalação isolada

1. Confirme que `D:\Jogos\GrandMP-Smoke\gta_sa.exe` é o Hoodlum de SHA-256 `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26` e que o Ultimate ASI Loader Win32 7.7.0 existente continua configurado com `DontLoadFromDllMain=1`. O arquivo INI, sozinho, não prova o contexto de execução.
2. Com o jogo fechado, guarde uma cópia do `GrandMP.asi` anterior e arquive o `D:\Jogos\GrandMP-Smoke\GrandMP.log` existente com nome único. O logger acrescenta linhas ao arquivo e não o limpa; use somente o log novo desta sessão. Copie **somente** `build\alpha027-win32\client\windows\Debug\GrandMP.asi` para `D:\Jogos\GrandMP-Smoke\GrandMP.asi`. Não copie o observador ou as DLLs de teste para o jogo. Registre o SHA-256 do ASI instalado para identificar exatamente a build testada.
3. Inicie o GTA manualmente. Aguarde o menu, depois consulte o **novo** `D:\Jogos\GrandMP-Smoke\GrandMP.log` e anote a linha `InitializeASI pid=... tid=...`, a versão e a confirmação Hoodlum. A ausência de linha ou identificação divergente é falha do smoke.
4. Com o jogo ainda aberto, execute o observador **fora da pasta GTA**: `build\alpha027-win32\client\windows\Debug\grandmp_window_observer.exe --pid <PID> --init-tid <TID>`. Use os valores decimais do log. Se houver mais de uma janela, mantenha todas as linhas; `candidata-principal` não é identificação definitiva.
5. Registre a saída completa, a fase do jogo no momento da observação (menu ou partida) e se os IDs coincidem. Encerre o jogo normalmente. Para rollback, com o jogo fechado, restaure o ASI anterior ou remova apenas o ASI de diagnóstico.

O Codex não executa essas etapas físicas nem copia arquivos. Os resultados devem ser trazidos para análise antes de qualquer timer, PIN, hook ou leitura de posição no GTA.

## Próximo mecanismo: avaliação do MinHook

O [MinHook v1.3.4](https://github.com/TsudaKageyu/minhook/tree/v1.3.4) é uma biblioteca C para x86/x64 sob [licença BSD de duas cláusulas, com avisos de HDE](https://github.com/TsudaKageyu/minhook/blob/v1.3.4/LICENSE.txt). Seu [código de hook](https://github.com/TsudaKageyu/minhook/blob/v1.3.4/src/hook.c) e [trampoline](https://github.com/TsudaKageyu/minhook/blob/v1.3.4/src/trampoline.c) tratam entrada de função: constroem trampoline e substituem prólogo por JMP. Isso **não intercepta diretamente** o CALL interno `0x53E981`.

Na ativação, MinHook enumera e tenta suspender outras threads, ajusta IPs, altera proteção de página e limpa cache de instruções. Na revisão v1.3.4, a falha de suspensão de uma thread individual pode ser ignorada; a implementação não prova quiescência completa no GTA. Também não comprova fingerprint Hoodlum nem propriedade exclusiva dos bytes diante de outros plugins. O README cita Visual Studio 2022; compilação com MSVC 2026 x86 ainda não foi validada neste repositório. Nenhum código ou dependência do MinHook foi incorporado.

**Recomendação:** não usar MinHook no CALL `0x53E981`. Uma futura avaliação pode estudar hook de entrada de uma função de atualização cujo endereço, prólogo, ABI e fase estejam verificados no Hoodlum, após comprovar inicialização fora do loader lock, residência, tratamento de falha de suspensão e ausência de conflito com outros plugins. A instalação com o jogo em execução permanece bloqueada até essas evidências. `SetTimer` também continua restrito ao host isolado da Alpha 0.2.6.
