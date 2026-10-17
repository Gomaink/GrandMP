# Cliente Windows

O alvo Win32 `grandmp_windows` produz `GrandMP.asi`. Ele registra versão, plataforma e diagnóstico conservador do executável em `GrandMP.log`. Somente o fingerprint exato de GTA SA 1.0 US Hoodlum é reconhecido; nenhuma variante está liberada para hooks ou leitura do jogador nesta etapa.

O bootstrap depende de Ultimate ASI Loader x86 com `DontLoadFromDllMain=1`. Essa configuração solicita ao loader chamar `InitializeASI` fora do loader lock; o contexto da chamada concreta no GTA ainda não foi comprovado. `DllMain` do GrandMP não inicializa o cliente.

O diagnóstico passivo de PID/TID e o observador externo estão descritos em [in-game-thread-diagnostics.md](../../docs/in-game-thread-diagnostics.md).

Fontes, decisões de compatibilidade, dados necessários e procedimento de smoke: [docs/windows-native.md](../../docs/windows-native.md).
