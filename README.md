# Google Drive Cloud Save Suite (Nintendo Switch Sysmodule & GUI)

Solução completa de backup automático de saves para o Google Drive no Nintendo Switch (Atmosphère CFW), composta por um **Daemon em segundo plano (Sysmodule)** de baixíssimo consumo de memória e um **Aplicativo de Gerenciamento (.NRO)** para a tela do console.

---

## Novidades da Versão 1.1.0
- **Gestão Inteligente de Pastas no Google Drive:**
  - Método `ensureFolderExists` que busca ou cria automaticamente a pasta destino (padrão: `Switch Saves`) pelo nome e armazena o `folder_id` em `config.json`.
  - **Uploads Sem Duplicatas (Zero Clutter):** Antes de enviar um arquivo, verifica se o save já existe na pasta. Se existir, executa requisição `PATCH` sobrescrevendo a versão anterior no Drive; se não existir, executa `POST` adicionando aos pais da pasta.
- **Aplicativo Gráfico Homebrew (`gdrive-sync.nro`):**
  - **Autorização na Tela (Device Flow):** Exibe a URL `https://www.google.com/device` e o código `user_code` na tela do console com polling animado até a confirmação do usuário.
  - **Teclado Nativo (`swkbd`):** Digite ou altere o `client_id`, `client_secret` e o nome da pasta de destino direto pelo Switch com o teclado oficial do Horizon.
  - **Controle do Sysmodule (`pmshell` / `pmdmnt`):** Inicie e pare o daemon em segundo plano com o toque de um botão ([X]) e ative/desative a inicialização automática no boot ([Y] via `boot2.flag`).

---

## Estrutura do Projeto

```text
├── Makefile                   # Orquestrador de build (compila Sysmodule e GUI em paralelo)
├── npdm.json                  # Permissoes e servicos do Sysmodule
├── toolbox.json               # Metadados para Atmosphere-Toolbox / Tesla Overlay
├── config.example.json        # Exemplo de credenciais OAuth2
├── README.md
├── source/                    # Codigo do Sysmodule e Core compartilhado
│   ├── main.cpp               # Daemon de background (heap 3MB, deteccao de jogos)
│   ├── Config.hpp             # Definicao de caminhos no SDMC e helpers de boot2.flag
│   ├── ProcessMonitor.hpp/.cpp# Monitoramento de ciclos de vida dos jogos (pm:dmnt/pm:info)
│   ├── SaveManager.hpp/.cpp   # Montagem de saves, deteccao de usuario e checagem de alteracoes (CRC32)
│   ├── ZipHelper.hpp/.cpp     # Compactador ZIP em stream direto para o SD (buffer de 64KB)
│   └── GDriveClient.hpp/.cpp  # OAuth2 Device Flow, ensureFolderExists, PATCH/POST uploads
└── gui/                       # Codigo do Aplicativo Homebrew (.NRO)
    ├── Makefile               # Build especifico para aplicacao NRO com libnx
    └── source/
        └── main.cpp           # Menu interativo ANSI, swkbd, polling OAuth e pmshell
```

---

## Pré-requisitos de Compilação

Certifique-se de ter o **devkitPro** configurado com as bibliotecas do Nintendo Switch:

```bash
# No MSYS2 do devkitPro (Windows) ou Linux:
dkp-pacman -S switch-dev switch-curl switch-mbedtls switch-zlib
```

---

## Como Compilar

Para compilar tanto o Sysmodule (`exefs.nsp`) quanto o aplicativo de interface (`gdrive-sync.nro`):

```bash
make
```

Para gerar o pacote pronto para copiar para a raiz do Cartão SD:

```bash
make dist
```

Os arquivos serão organizados automaticamente na pasta `./dist`:
- `dist/atmosphere/contents/4200000000000010/exefs.nsp` (Sysmodule Daemon)
- `dist/atmosphere/contents/4200000000000010/toolbox.json` (Info Atmosphere-Toolbox)
- `dist/atmosphere/contents/4200000000000010/flags/boot2.flag` (Auto-boot ativado)
- `dist/switch/gdrive-sync/gdrive-sync.nro` (App Homebrew de Gerenciamento)
- `dist/config/gdrive-sync/config.example.json`

---

## Como Obter as Credenciais Google OAuth 2.0

1. Acesse o [Google Cloud Console](https://console.cloud.google.com/).
2. Crie um projeto novo (ex: `Switch Cloud Save`).
3. Vá em **APIs e Serviços > Biblioteca** e ative a **Google Drive API**.
4. Vá em **APIs e Serviços > Tela de Permissão OAuth**:
   - Tipo de usuário: **Externo**.
   - Escopo: adicione `https://www.googleapis.com/auth/drive.file`.
   - Usuários de teste: adicione sua conta Google.
5. Vá em **APIs e Serviços > Credenciais**:
   - Clique em **Criar Credenciais > ID do cliente OAuth**.
   - Tipo de aplicativo: selecione **TVs e dispositivos com entrada limitada** (TVs and Limited Input devices).
6. Copie o `Client ID` e o `Client Secret`.
7. Você pode inseri-los no arquivo `sdmc:/config/gdrive-sync/config.json` ou digitá-los diretamente pelo teclado virtual no aplicativo do Switch!

---

## Certificados SSL (CA Bundle)

Para que o `libcurl` valide as requisições HTTPS para a Google API:
- Baixe o bundle de certificados oficial da Mozilla: [cacert.pem](https://curl.se/ca/cacert.pem)
- Coloque em `sdmc:/config/gdrive-sync/cacert.pem` (ou `sdmc:/switch/cacert.pem`).

---

## Instalação e Uso no Nintendo Switch

1. Copie o conteúdo da pasta `dist/` para a raiz do seu cartão SD.
2. Inicie o console e abra o **Homebrew Menu** (pelo Álbum ou segurando R em um jogo).
3. Abra o aplicativo **Google Drive Sync**:
   - Se ainda não preencheu o `config.json`, use as opções `4` e `5` para digitar o Client ID e Client Secret usando o teclado na tela.
   - Selecione a opção `1. Vincular Conta Google`:
     - O Switch exibirá na tela a URL `https://www.google.com/device` e o código de ativação.
     - Acesse pelo celular, digite o código e aprove.
     - O Switch detectará a autorização imediatamente e salvará os tokens.
   - Pressione `[X]` para iniciar o Sysmodule em segundo plano ou `[Y]` para manter a inicialização automática no boot.
4. Pronto! Sempre que você fechar qualquer jogo, o Daemon verificará se o save foi alterado e atualizará o arquivo correspondente na pasta **Switch Saves** do seu Google Drive.
