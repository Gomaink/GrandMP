# GrandMP Alpha 0.13.1 — Native Lifecycle

Data: 2026-11-04
Baseline: b94ef54 (Alpha 0.11.2)
Ambiente: GTA San Andreas 1.0 US Hoodlum, GTA A isolado.

## Resultado fisico

- F6 criou o CPlayerPed remoto independente.
- O personagem caminhou, virou e parou.
- 397 processamentos nativos consecutivos foram registrados.
- F8 armou uma solicitacao unica de remocao.
- O lifecycle concluiu com `tick.removed`.
- O segundo CJ desapareceu visualmente.
- O CJ local permaneceu funcional.
- O GTA encerrou normalmente.
- GTA A restaurado a baseline; GTA B permaneceu intacto.

## Evidencia

Processo: PID 4476

Log:
`GrandMPPlayerBootstrap.Diagnostic.4476.01DD6BFDAEEF66C4.log`

SHA-256 do log:
`6B59481E21CD9A698820FB5BC32C26F5F4C3F913EB5FE8A5E1D789CCCFE656FF`

SHA-256 do CLEO validado:
`0F95F3BB33A29BF0DC5B0C7BEF2F2918EC8A6BE3EEEBB934A76FFCB20C1DCA9C`

## Limites

O teste comprova um ciclo funcional de criacao, movimentacao
e remocao em uma sessao real do Hoodlum.

Nao comprova ausencia de vazamentos, seguranca de repeticoes
prolongadas ou comportamento sob todas as transicoes de streaming.

O replay permanece fail-closed. O teste nao inclui
sincronizacao entre dois clientes reais pela rede.
