# Alpha 0.3.6 — READY físico e fechamento local

## Evidência física preservada

Execução manual do usuário: `build/physical-ready-smoke/20261014-210326-3078b080`, PID **18604**, criação `2026-10-15T00:03:31.8645237Z` (14/10 no horário local). O usuário confirmou abertura normal, carregamento de partida e movimentação de CJ.

| Avaliação | Resultado |
|---|---|
| Observação do bootstrap dentro do GTA | **PASS**: 111 amostras READY, PIN=1, count=0, callbackCapable=0, installed=0; bootstrap TID 16964 |
| Primeira amostra do diagnóstico | INCONCLUSIVA, módulo ainda ausente/ambíguo; seguida das 111 amostras válidas |
| Transição REQUESTED → READY diretamente observada | **Não**; `TransitionDirectlyObserved=false`; nenhuma amostra REQUESTED |
| Resultado automatizado original | **INCONCLUSIVO / CLEANUP_FAILED**, erro `CLEANUP_GAME_STILL_RUNNING` |
| Exit code registrado | 13; isoladamente não prova crash nem sucesso |
| Recuperação manual | Usuário confirmou ausência do processo, verificou SHA-256 e removeu o experimental |
| Arquivos originais | 13/13 preservados, conforme relatório e nova conferência somente leitura nesta tarefa |
| Callback/patch | Nenhum instalado pelo experimento; flags de instalação/capacidade zero |

O resultado original **não foi reclassificado**. A classificação PASS da observação física é separada do encerramento/cleanup inconclusivo da automação. Recuperação posterior não transforma o relatório antigo em PASS.

Proveniência SHA-256 dos relatórios, que permanecem ignorados em `build/`:

| Arquivo | SHA-256 |
|---|---|
| `result.json` | `3929E5AA76536FED98AA9AE871F750AD7B91EEEDEA3817A538AEB4E3A1B7B600` |
| `launch.json` | `D234682712797FDFE44DD9AA4D069937289BD9A7F95C5B5B608ACE5B8BB04080` |
| `diagnostic.jsonl` | `8E2542CE777C130256D8607628CA0D6DB56FDF4184BD6B41CE2BFB8AC80EF183` |
| `preflight.json` | `951EDA77C4407CAF83308E8A9648658257DD2057FDE1D037ED29988C1905EC54` |

## Causa do cleanup

A automação antiga aguardava o handle iniciado e depois executava uma segunda condição: qualquer objeto retornado por `Get-Process` com `ProcessName=gta_sa` impedia a limpeza imediatamente. Esse teste não consultava estado vivo, imagem, horário de criação ou identidade da sessão; também não oferecia janela limitada para término tardio/atualização da enumeração.

A condição que lançou a exceção é identificável no código. O ator exato **não**: o log não guardou IDs/horários/estado dos candidatos da enumeração. Um objeto de processo encerrado ainda enumerado, término em andamento ou outro processo homônimo são hipóteses; não declarar nenhuma como fato físico comprovado. A confirmação posterior do usuário de que não havia GTA ativo não reconstrói aquela enumeração.

A correção limita a espera, distingue sessão por PID/caminho/criação, não trata enumeração homônima como prova isolada de execução e conserva a recusa de remover diante de instância relevante viva ou inacessível. Não encerra o GTA nem usa o exit code 13 como classificador. Detalhes e recuperação: [physical-ready-smoke.md](physical-ready-smoke.md).

## Histórico local coerente

Baseline: `af3af1c90c61216dfc153a2f51b14f2368e2c8bb`. As propostas antigas de commits em relatórios das slices continuam como registros históricos. O fechamento usa **três commits consolidados**:

1. **Bridge de replicação**: Slice 2 e suas correções de ordenação/reset; quatro hunks CMake de bridge/adapter e seus testes.
2. **Experimentos nativos**: Slices 1/3/4/5/6, counter, wrapper, writer exclusivo do laboratório, READY/preflight/diagnóstico e bootstrap assíncrono; restantes hunks CMake.
3. **Smoke físico/cleanup**: launcher manual, regressões offline e documentação desta evidência física.

Os fontes counter/host/READY foram evoluindo nos mesmos arquivos e não há snapshots intermediários confiáveis para separar todas as slices sem reconstrução artificial. Bridge e experimentos têm dependências independentes; os scripts físicos dependem dos artefatos nativos. Assim se preserva rastreabilidade sem inventar seis estados históricos não conservados.

Somente fontes, testes, scripts e documentação entram nos commits. Builds, executáveis/ASI/PDB, checkouts de pesquisa, relatórios/logs brutos e arquivos GTA ficam ignorados. Não há licença provisória ou código de plugin-sdk/loader copiado; as referências/revisões e limites já estão documentados nas slices.

## Gates de produto

READY/PIN físico está observado, mas **callback contador e patch no GTA continuam sem homologação/autorização de execução nesta tarefa**. Alpha 0.3.6 permanece aberta para os requisitos restantes do roadmap: experimento contador separado, contexto/ABI do wrapper, budgets/soak/ciclos e ponte N2. Nenhum ped foi implementado; não há avanço automático.

As validações finais e hashes dos commits são apresentados no handoff de fechamento. A árvore validada inclui as regressões Win32/x64 e testes PowerShell 7/5.1; nenhum GTA foi executado durante a correção/commit.
