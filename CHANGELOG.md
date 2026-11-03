# Changelog

Todas as alterações notáveis deste projeto são documentadas neste arquivo.

O formato baseia-se em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.0.0/) e este projeto adere ao [Semantic Versioning](https://semver.org/lang/pt-BR/).

## [0.1.0] - 2026-11-03

### Added
- Interface gráfica com renderização vetorial utilizando a fonte oficial compartilhada do Horizon OS (`pl:u`, `PlSharedFontType_Standard`) via `stb_truetype`.
- Resolução e escala dinâmica (1280x720 Handheld / 1920x1080 Docked) adaptada via `appletGetOperationMode()`.
- Extração oficial de metadados e ícones JPEG de jogos via serviço `ns:app` (`nsGetApplicationControlData`), com cache de texturas permanente em memória ($O(1)$).
- Restauração transacional em `save:/` com geração prévia de backup local de segurança (`<TitleID>_before_restore_<ts>.zip`) e rollback automático.
- Trava de segurança temporal RTC (`MIN_SANE_RTC_TIMESTAMP`): bloqueio preventivo de restaurações automáticas caso o relógio do Switch esteja descalibrado (< 2025).
- Suíte automatizada de testes cobrindo testes unitários em C++ (Catch2), testes de integração de API (Pytest) e cenários de recuperação de desastres (queda de pacotes e corrupção).
- Arquitetura de código em camadas desacopladas (`core`, `network`, `storage`, `ui`, `utils`) com gerenciamento de recursos via RAII estrito.

### Changed
- Menu principal reestruturado em layout de 2 colunas: painel lateral com status real e painel de ações de rotina.
- Tela de configurações isolada da navegação diária (acessível via `[-]` ou `[Y]`).
- Processamento pesado de I/O de disco e transferências de rede delegado para thread de trabalho assíncrona, assegurando 60 FPS contínuos no loop de UI.
- Cliente de rede padronizado com timeouts progressivos (`CURLOPT_CONNECTTIMEOUT`, `CURLOPT_LOW_SPEED_LIMIT`) e callback de cancelamento gracioso via botão `(B)`.
- Documentação técnica consolidada em padrão neutro e profissional.

### Removed
- Remoção definitiva de métricas, textos promocionais e jargões defensivos da interface e da documentação.

---

## [0.0.8] - 2026-10-25

### Changed
- Transição da arquitetura para operação 100% sob demanda: desativação do sysmodule persistente em segundo plano, liberando o System Memory Pool do Atmosphère (`pool_partition: 2`) e eliminando disputas de heap com outros overlays do sistema.
- Execução de tarefas de backup e restauração consolidada na aplicação gráfica Homebrew (`.nro`).

---

## [0.0.7] - 2026-10-18

### Added
- Renovação proativa do token OAuth 2.0 antes de requisições de listagem e envio na Google Drive API v3.
- Ordenação estrita de consultas por data de criação (`&orderBy=createdTime`), prevenindo a criação indevida de pastas duplicadas no Drive.
- Recuperação automática de arquivos presentes na lixeira através de `PATCH {"trashed": false}`.
- Sincronização em Lockstep: persistência paralela do arquivo raiz (`save_<TitleID>.zip`) e do snapshot histórico imutável (`<TitleID>_<AccountUid>_<TimestampUTC>_<CRC32>.zip`).

---

## [0.0.6] - 2026-10-10

### Added
- Implementação da máquina de estados determinística (`SyncState`) para controle de ciclo de vida de operações de save.
- Integração ao fluxo OAuth 2.0 Device Authorization Grant (RFC 8628), com pareamento via código em `google.com/device`.
- Buffers de compactação zlib em streaming contínuo padronizados em blocos de 32 KB para redução de consumo de memória.
