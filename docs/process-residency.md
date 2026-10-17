# Alpha 0.2.5 — residência do GrandMP e preparação do CALL

## Escopo e decisão

O ASI continua sem hook, sem chamada a `FindPlayerPed`/`FindPlayerCoors` e sem callback do GTA. Esta etapa implementa e testa a política de residência fora do GTA, mas **não a conecta ao `InitializeASI` de produção**: o contexto da chamada não foi comprovado. Nenhuma instrução do jogo é modificada.

O [contrato Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw) afirma que `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN)` fixa uma DLL até o término do processo, mesmo com chamadas a `FreeLibrary`. PIN é irreversível durante a execução. A política `ModuleResidency` exige identidade Hoodlum e do loader, confere que o endereço pertence ao handle do próprio módulo com uma referência temporária e impede segunda tentativa. Essa referência é liberada após a tentativa; ela não é o PIN. O backend Win32 foi exercitado em processo CTest isolado.

A [fonte da tag v7.7.0](https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/v7.7.0/source/dllmain.cpp) lê `dinput8.ini`, `global.ini`, `scripts\global.ini`, `plugins\global.ini` e `update\global.ini` nessa ordem; valores posteriores prevalecem. Os dois INIs encontrados na instalação isolada declaram `DontLoadFromDllMain=1`. Ler esses arquivos de novo no ASI não prova que a chamada concreta ocorreu fora do loader lock: o arquivo pode mudar após a leitura do loader, ou outro mecanismo pode invocar a exportação. Por isso o ASI **não chama PIN**, mesmo quando os fingerprints coincidem. Esse é um bloqueio de segurança pendente para a próxima investigação.

O endereço usado fica na variável `moduleHandle` do próprio GrandMP. Antes do PIN, uma chamada `FROM_ADDRESS` sem `PIN` adquire uma referência temporária e o handle resultante deve ser igual ao handle recebido em `DllMain`. Depois do PIN, o handle retornado é conferido novamente. Módulo errado, host desconhecido, loader desconhecido ou falha da API deixam a integração sem residência confirmada e sem possibilidade de hook. Um resultado inesperado após PIN é irreversível; a política marca falha e não prossegue.

O filtro inicial de compatibilidade exige:

| Componente | Identidade aceita para esta experiência |
| --- | --- |
| GTA | PE32/i386, 14.383.616 bytes, SHA-256 `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26` |
| ASI Loader | `dinput8.dll` carregado da pasta do executável, arquivo com SHA-256 `29FEE0AA46414864AD1F65B2689B470BFB2CF2F09A94E2AFD271E68F2E16743B`, observado com VersionInfo 7.7.0.0. O binário local ainda não foi comparado byte a byte com o asset oficial. |
| Sessão | `D:\Jogos\GrandMP-Smoke`, um GrandMP, nenhum outro plugin ASI ativo, sem atualização ou hot unload. São critérios operacionais; o ASI não os detecta automaticamente. Nenhum PIN de produção é permitido. |

O fingerprint do arquivo do loader é uma restrição adicional ao [fonte da tag v7.7.0](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/v7.7.0/source/dllmain.cpp), analisado em [hook-lifecycle.md](hook-lifecycle.md). A confirmação do arquivo em disco e do módulo com o caminho esperado não prova identidade byte a byte da imagem já carregada; por isso nenhum hook é liberado por essa verificação. Outros nomes de proxy, versões e builds do loader permanecem fora deste primeiro cenário.

## Propriedade e encerramento

`InitializeASI` conserva a guarda de inicialização única. O `WindowsClientRuntime` temporário possui adaptador, `CallbackGate` e política de posição desativada. Ele registra o diagnóstico e é destruído ao retornar, sem callback pendente, PIN ou patch. O desenho futuro exige runtime único com duração de processo **somente após** comprovar que o ponto de entrada é seguro e que o módulo não poderá ser descarregado. Não há suporte a hot unload.

O `HookManager` sintético da 0.2.4 não integra o runtime do ASI. Sua guarda `std::terminate` continua exclusiva dos testes de contrato; um registro ativo no encerramento do ASI não pode dispará-la. A desativação lógica do diagnóstico futuro deverá preservar runtime, código e registro de callback, sem restaurar CALL em `DllMain`. O `CallbackGate::Lease` atual mantém somente o gate vivo; um callback real ainda precisará acessar apenas objetos de duração de processo e obedecer a um protocolo de encerramento.

## Plano para um único CALL real — ainda bloqueado

No Hoodlum de hash aprovado, `0x53E981` contém a instrução completa `E8 5A D5 FF FF`, `CALL rel32 0x53BEE0`, com retorno em `0x53E986`. A análise de ABI e o contexto do frame estão em [hoodlum-player-position.md](hoodlum-player-position.md). Um futuro interceptador precisa verificar esses bytes **na memória do processo imediatamente antes da instalação**, manter a semântica de chamada sem argumentos naquele ponto, preservar os registradores e a pilha conforme o contrato x86, encaminhar obrigatoriamente a chamada original e executar o diagnóstico somente depois, quando houver jogador válido. Ausência de jogador, carregamento ou desativação lógica devem produzir apenas a chamada original. Nenhum trampoline ou escrita foi implementado.

[VirtualProtect](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect) pode permitir escrita na página, e [FlushInstructionCache](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-flushinstructioncache) é responsabilidade do código que altera instruções. **Nenhuma dessas APIs garante que outro thread não execute uma instrução de cinco bytes parcialmente reescrita.** Comparação de bytes e mutex do GrandMP também não oferecem essa garantia. Suspender threads arbitrariamente traz risco de deadlock ou suspensão em estados inadequados e não foi implementado.

Foram avaliados mecanismos existentes, sem incorporá-los: [Detours](https://github.com/microsoft/Detours/wiki/DetourUpdateThread) exige inscrever threads na transação e alerta que threads não inscritas podem executar mistura de código antigo e novo; [MinHook](https://github.com/TsudaKageyu/minhook) é voltado a hooks de entrada de função; [SafetyHook](https://github.com/cursey/safetyhook/blob/main/README.md) oferece mid-hooks, mas exige revisão da ordem em relação ao CALL, relocação de instruções, duração do objeto de hook, dependências e compatibilidade C++20. Nenhum foi comprovado para o CALL único Hoodlum. A instalação real permanece **no-go** até um mecanismo auditado demonstrar quiescência ou transação segura nesse call-site e até os pré-requisitos de estado do jogo serem validados.

## Cobertura de testes e limite

Os testes com API falsa verificam uma tentativa, rejeição de jogo/loader/módulo desconhecido, falha de PIN sem repetição e ausência de limpeza automática. Em Win32, um processo CTest isolado carrega o `GrandMP.asi` do diretório de build, não chama `InitializeASI`, fixa esse módulo com identidades fornecidas pelo próprio teste e verifica que ele permanece carregado após `FreeLibrary`. O teste termina em seguida; ele prova a semântica da API naquele processo, **não** a execução do PIN dentro do GTA. O teste de entrada ASI usa um host desconhecido e confirma que `FreeLibrary` descarrega o módulo. Nenhum ASI é copiado para o jogo ou para outro diretório pelo trabalho desta etapa.
