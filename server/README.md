# Servidor

A Alpha 0.3.1 produz `grandmp_server`, servidor UDP experimental com bind somente em `127.0.0.1`, porta 7777 por padrão (`--port` para alterar). A tabela de sessões fica separada do loop de sockets e usa o protocolo portátil de `shared/`. Consulte [a especificação e o smoke manual](../docs/first-network-transport.md). Não há autenticação, persistência ou acesso público.
