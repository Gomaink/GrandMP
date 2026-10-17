# Physical READY Smoke — execução manual

O agente prepara/testa o launcher; **o usuário executa o GTA**. Esta entrega não executou GTA nem instalou o experimental na instalação isolada.

## Um comando

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\scripts\Invoke-PhysicalReadySmoke.ps1'
```

Compatível com Windows PowerShell 5.1 e PowerShell 7. O script confere os hashes fixados do Hoodlum, UAL Win32 7.7.0, GrandMP homologado, experimental e diagnóstico, PE32/base e inventário ASI; confirma Windowed Mode desabilitado. Rebuild que alterar fingerprint bloqueia o preflight; não aceitar automaticamente outro binário.

Depois do preflight, digite **SIM** para autorizar instalação temporária e uma sessão. O opt-in existe somente no ambiente do processo filho; não há variável persistente/global. Antes da confirmação não há mudança na instalação nem execução do jogo.

Espere a mensagem `READY PIN=1 contador=0 callbackCapable=0 installed=0`. Verifique se o GTA continua funcionando e saia normalmente pelo menu. O script não mata o GTA. Após o encerramento, ele remove apenas o experimental verificado, reconfere os 13 originais e pergunta se o jogo funcionou e saiu normalmente. Responda `SIM`, `NAO` ou `INCONCLUSIVO`.

O diagnóstico usa a interface existente `--pid <decimal> --expected-asi <arquivo>`; nenhuma chamada ao motor, debugger, writer, patch ou injeção. O tempo padrão para atingir READY é 30 segundos (`-ReadyTimeoutSeconds` permite 1–300); o diagnóstico tem limite próprio de 5 segundos. Só o subprocesso de diagnóstico pode ser encerrado se travar. Perda de observação segura após READY interrompe a coleta e impede PASS.

## Relatórios

Cada execução cria `build/physical-ready-smoke/<data-hora>-<id>/`:

- `preflight.json`: hashes, 13 originais, PE/base, loader e inventário, ou razão de bloqueio.
- `launch.json`: PID e timestamp UTC da sessão. Identidade viva é consultada por handle/API Windows e comparada por ticks DateTime, não por strings JSON.
- `diagnostic-raw.jsonl`: saída/exit code preservados antes da interpretação.
- `diagnostic.jsonl`: campos e estados interpretados.
- `result.json`: PASS/FAIL/INCONCLUSIVO, motivo, READY/PIN/counter/flags, exit code, opinião do usuário, hashes finais e limpeza.
- `exception.json`/`controller-error.json` e log de timeout, quando necessários.

PASS exige READY válido do PID correto, PIN declarado pelo descriptor do binário admitido, contador zero, flags zero, saída do processo, confirmação visual do usuário e limpeza/hashes aprovados. Códigos de saída 1 ou 13 sozinhos não significam crash nem sucesso; são registrados, sem substituir a observação do usuário.

FAILED/SHUTDOWN ou campos inseguros resultam FAIL. Timeout, encerramento antes de READY, leitura ambígua/exceção ou limpeza não comprovada impedem PASS com motivos distintos. Polling pode começar em READY: `TransitionDirectlyObserved` informa se REQUESTED também foi observado; nenhuma transição física não amostrada é inventada. READY/PIN não autoriza callback ou patch.

## Recuperação

Em timeout/erro, preserve os relatórios e saia pelo menu. O script aguarda a saída; não tenta corrigir o jogo. Não feche a janela PowerShell enquanto aguarda.

Se a execução for interrompida ou a limpeza falhar, `CleanupPending` e `Recovery` identificam o experimental. **Feche todas as instâncias GTA antes de removê-lo manualmente.** Verifique que o caminho é exclusivamente `D:\Jogos\GrandMP-Smoke\scripts\GrandMPNativeCounterExperimental.asi` e que corresponde ao hash admitido; não substitua/remova GrandMP.asi. O script não remove arquivo alterado ou alvo/diretório reparse.

Envie `preflight.json`, `result.json` e os logs de diagnóstico/exceção. Não envie executáveis ou assets GTA.

## Validação offline desta entrega

`scripts/tests/Test-PhysicalReadySmoke.ps1`: **76 asserções / nove cenários aprovados em PS7 e PS5.1**, utilizando o host contador existente e o diagnóstico real, sem GTA. Inclui PID/caminho/timestamp incorretos; regressão ISO/string/DateTime UTC/local; timeout; saída antecipada; exceção de diagnóstico; limpeza falha; observação inconclusiva; contador inseguro; perda de leitura depois de READY. Todos os cenários verificam razões específicas e preservação dos 13 arquivos simulados. Falha de limpeza conserva somente o artefato da fixture e registra recuperação pendente.

Evidências: `build/physical-ready-smoke-tests/8f3fc253d778449baeefce0a99af60a4` (PS7) e `673fbe05538f4ea7b15875c619588e9a` (PS5.1). Preflight `-CheckOnly` também passou sem instalar/iniciar GTA. Windows native e reviewer realizaram revisão focada.

O modo `-CheckOnly` gera relatórios e retorna INCONCLUSIVO/`CHECK_ONLY_NO_GAME_STARTED`, pois não constitui smoke físico. Os scripts podem ser testados offline pelo arquivo de testes; o comando público não oferece bypass da confirmação SIM nem perfil de laboratório.

## Evidência física posterior

O usuário executou o smoke em `20261014-210326-3078b080`: **PASS para observação READY/PIN**, com partida e movimentação de CJ, count=0/callbackCapable=0/installed=0; **INCONCLUSIVO para o resultado automatizado original**, devido ao cleanup. A recuperação manual removeu o experimental e confirmou 13/13 hashes. A transição REQUESTED→READY não foi diretamente amostrada. Relatório original preservado, causa e hashes auditáveis: [alpha036-closeout.md](alpha036-closeout.md).


## Revalidacao offline apos correcoes manuais

Apos o reparo do cleanup, o registro independente do GameExitCode
e a protecao contra regressao de READY:

- 120 assercoes por ambiente, em 12 cenarios.
- PowerShell 7: PASS.
- Windows PowerShell 5.1: PASS.
- READY -> REQUESTED: FAIL esperado.
- Cleanup bloqueado por processo vivo/inacessivel: comportamento esperado.
- Nenhum GTA executado nesta revalidacao.

Evidencias:
- PS7: build/physical-ready-smoke-tests/1d43c85eadd14318a764fa29cfc1838b
- PS5.1: build/physical-ready-smoke-tests/117a2c12d8094d4a80647ace4f9c634e

O resultado automatizado do smoke fisico original permanece INCONCLUSIVO.
A observacao fisica READY/PIN e a recuperacao manual continuam registradas
separadamente. Esta validacao offline nao homologa callback ou patch no GTA.
