# Alpha 0.2.11 — observador externo de posição

`grandmp_position_observer.exe` é um processo Win32 separado do `GrandMP.asi`. Ele não injeta código, não instala hooks, não escreve na memória do jogo e não usa WinDbg. O leitor consulta o processo aproximadamente a cada 200 ms. O primeiro teste com GTA real depende de execução manual em `D:\Jogos\GrandMP-Smoke`.

## Identidade e limites

O programa enumera `gta_sa.exe` com Toolhelp. Se houver mais de um processo, exige `--pid`. Abre o PID somente com `PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION`, obtém o caminho por `QueryFullProcessImageNameW` e confere que o módulo principal está em `0x00400000`. Lê o arquivo desse caminho, valida PE32/i386, tamanho de 14.383.616 bytes e SHA-256 `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26` pelo detector existente. Qualquer falha recusa a leitura.

A verificação do hash corresponde ao arquivo no caminho informado pelo processo. Ela não é uma prova byte a byte da imagem já carregada: outra ferramenta poderia substituir o arquivo ou modificar a memória depois do carregamento. A homologação do layout do plugin-sdk também não substitui a observação física da posição. O teste manual deve confirmar que os números correspondem a CJ. Outras versões e bases do GTA são recusadas.

## Layout consultado

Fonte: [plugin-sdk, revisão `15f15b60bbf74c106e1b496ff92c98764abf4605`](https://github.com/DK22Pac/plugin-sdk/tree/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa), licença zlib; apenas valores de endereço e offset foram usados, sem incorporar código de terceiros.

| Componente | Evidência na revisão | Leitura externa |
| --- | --- | --- |
| [CWorld.cpp](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/CWorld.cpp) | `Players = (CPlayerInfo*)0xB7CD98` | O primeiro elemento começa nesse endereço; ler diretamente o ponteiro de ped do jogador 0. |
| [CPlayerInfo.h](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/CPlayerInfo.h) | `m_pPed` no offset `0x00` | Ponteiro x86 de 32 bits ou nulo. |
| [CPlaceable.h](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/CPlaceable.h) | `m_placement` em `0x04`; `m_matrix` em `0x14` | Ler ponteiro da matriz a partir do ped. |
| [CMatrix.h](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/CMatrix.h) e [CMatrixLink.h](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/CMatrixLink.h) | `CMatrix::pos` em `0x30`; `CMatrixLink` estende `CMatrix` | Se a matriz existir, ler três `float` em `matrix + 0x30`. |
| [CSimpleTransform.h](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/plugin_sa/game_sa/CSimpleTransform.h) | `m_vPosn` em `0x00`, tamanho `0x10` | Com matriz nula, ler três `float` em `ped + 0x04`. |

O leitor recusa ponteiros nulos ou implausíveis, falha ou leitura parcial de `ReadProcessMemory` e coordenadas NaN/infinito. Reler os ponteiros do ped e da matriz depois dos valores detecta algumas trocas durante o polling, mas não produz uma amostra atômica do processo. Em transições do mundo, o estado pode ser temporariamente indisponível. A posição do ped em veículo pode diferir da posição visual do veículo; isso deve ser observado no smoke.

## Compilação e testes

```powershell
cmake -S . -B build/alpha0211-win32 -G "Visual Studio 18 2026" -A Win32
cmake --build build/alpha0211-win32 --config Debug
ctest --test-dir build/alpha0211-win32 -C Debug --output-on-failure

cmake -S . -B build/alpha0211-x64 -G "Visual Studio 18 2026" -A x64
cmake --build build/alpha0211-x64 --config Debug
ctest --test-dir build/alpha0211-x64 -C Debug --output-on-failure
```

O executável é criado somente no build Win32: `build/alpha0211-win32/client/windows/Debug/grandmp_position_observer.exe`. O leitor e seus testes também compilam em x64; o ASI existente não foi alterado.

## Smoke manual na instalação isolada

1. Em um PowerShell, execute o observador antes do jogo:

   ```powershell
   & 'D:\Programacao\Workspace\Applications\grandmp\build\alpha0211-win32\client\windows\Debug\grandmp_position_observer.exe'
   ```

2. Inicie `D:\Jogos\GrandMP-Smoke\gta_sa.exe` normalmente, sem WinDbg. Aguarde `Hoodlum homologado`, seguido de `GTA identificado; aguardando jogador` no menu. Se houver vários `gta_sa.exe`, o observador lista os PIDs e encerra; reinicie-o com `--pid <PID>`.
3. Carregue uma partida. Com CJ parado, confirme X, Y e Z finitos, com três casas decimais e estáveis. Caminhe e confira que mudam de modo coerente. Se testar veículo, anote se os valores acompanham o ped ou a posição visual do veículo.
4. Saia da partida e depois do jogo. Confirme que o observador mostra ausência temporária do jogador ou do processo, sem travar. Pressione Ctrl+C para encerrar o observador normalmente.

Se aparecer `recusado`, confira o SHA-256 do arquivo, a base, a arquitetura e o PID. Se aparecer `ReadProcessMemory falhou`, `leitura parcial` ou `ponteiro alterado`, registre a fase do jogo e a mensagem exata. Se as coordenadas forem estáveis mas incorretas, compare com a posição visível de CJ e forneça as amostras antes e depois de caminhar; os testes sintéticos não provam a correção do layout em Hoodlum real.
