# gdrive-sync

Cliente Homebrew para backup e restauração de dados de saves do Nintendo Switch no Google Drive.

<p align="center">
  <img src="https://img.shields.io/badge/Vers%C3%A3o-v0.1.1-34A853?style=flat-square" alt="Versão v0.1.1" />
  <img src="https://img.shields.io/badge/Plataforma-Nintendo%20Switch-E60012?style=flat-square" alt="Nintendo Switch" />
  <img src="https://img.shields.io/badge/CFW-Atmosph%C3%A8re%201.7%2B-00D2FF?style=flat-square" alt="Atmosphere" />
  <img src="https://img.shields.io/badge/Licen%C3%A7a-GPL--3.0-blue?style=flat-square" alt="Licença GPL-3.0" />
</p>

<p align="center">
  <img src="screenshot.jpg" alt="Interface Gráfica do gdrive-sync" width="800" />
</p>

---

## Funcionalidades

- **Backup Completo e Seletivo:** Compactação em blocos de 32 KB e sincronização de saves em estrutura Lockstep (arquivo raiz mutável `save_<TitleID>.zip` e snapshots históricos imutáveis versionados por timestamp e CRC32).
- **Restauração Transacional com Rollback:** Valida a integridade do arquivo antes da extração. Cria um backup preventivo no cartão SD antes de sobrescrever a partição `save:/` e reverte os arquivos originais automaticamente caso ocorra falha de descompressão ou erro de commit do Horizon OS.
- **Trava de Sanidade RTC (`MIN_SANE_RTC_TIMESTAMP`):** Previne sobrescritas indevidas bloqueando restaurações automáticas caso o relógio do console esteja descalibrado (< 2025).
- **Autenticação OAuth 2.0 Nativa (Device Flow):** Pareamento seguro via código de ativação em `google.com/device` (RFC 8628), sem necessidade de inserir senhas diretamente no console.
- **Tratamento de Lixeira:** Detecta saves movidos para a lixeira do Google Drive e executa restauração automática via API (`PATCH {"trashed": false}`), prevenindo duplicatas.
- **Interface Gráfica Horizon OS:** Renderização vetorial fluida a 60 FPS com suporte dinâmico a 1280x720 (Modo Portátil) e 1920x1080 (Modo Dock), com extração de ícones oficiais dos títulos via serviço `ns:app`.
- **Execução Assíncrona Não-Bloqueante:** Processamento de I/O e transferências em thread dedicada, com suporte a cancelamento imediato a qualquer momento pelo botão `(B)`.

---

## Requisitos

- Nintendo Switch desbloqueado rodando **Atmosphère 1.7.0 ou superior**.
- Conexão Wi-Fi ativa.
- Bloqueio de telemetria da Nintendo configurado (DNS MITM via `atmosphere/hosts/default.txt` ou 90DNS).
- Projeto ativo no Google Cloud Console com a API do Google Drive habilitada.

---

## Instalação

Copie os arquivos para o cartão SD mantendo a seguinte estrutura:

```text
sdmc:/
 ├── switch/
 │    └── gdrive-sync/
 │         └── gdrive-sync.nro          # Aplicativo Homebrew executável
 └── config/
      └── gdrive-sync/
           ├── config.json              # Credenciais OAuth e configurações locais
           └── cacert.pem               # Certificados de autoridade CA para validação TLS
```

> [!TIP]
> Execute o aplicativo preferencialmente via **Title Override** (segurando o botão `[R]` ao abrir qualquer jogo instalado) para disponibilizar o heap completo de memória para o Homebrew Menu.

---

## Configuração Inicial

### Passo 1: Obter Credenciais no Google Cloud Console

1. Acesse o [Google Cloud Console](https://console.cloud.google.com/) e crie um novo projeto (ex: `Switch Cloud Save`).
2. Acesse **APIs e Serviços** > **Biblioteca**, procure por **Google Drive API** e clique em **Ativar**.
3. Acesse **APIs e Serviços** > **Tela de consentimento OAuth**:
   - Selecione o tipo de usuário **Externo** e clique em **Criar**.
   - Preencha o nome da aplicação e o seu e-mail de contato.
   - Na aba **Escopos**, adicione o escopo: `https://www.googleapis.com/auth/drive.file`.
   - Na aba **Usuários de teste**, adicione o endereço de e-mail da sua conta Google.
4. Acesse **APIs e Serviços** > **Credenciais**:
   - Clique em **+ Criar Credenciais** > **ID do cliente OAuth**.
   - No campo **Tipo de aplicativo**, selecione **TVs e dispositivos de entrada limitada**.
   - Copie o **Client ID** e o **Client Secret** gerados.

### Passo 2: Inserir Credenciais no Console

Insira os valores obtidos no arquivo `sdmc:/config/gdrive-sync/config.json`:

```json
{
  "client_id": "SEU_CLIENT_ID.apps.googleusercontent.com",
  "client_secret": "SEU_CLIENT_SECRET",
  "folder_name": "Switch Saves",
  "folder_id": "",
  "access_token": "",
  "refresh_token": ""
}
```

*Alternativamente, o Client ID e Client Secret podem ser configurados diretamente na tela de Configurações do aplicativo (`[-]` ou `[Y]`).*

### Passo 3: Parear com o Dispositivo

1. Abra o **gdrive-sync** pelo Homebrew Menu.
2. Na tela principal, selecione **Vincular Conta Google**.
3. Um código de 8 dígitos será exibido na tela junto com o endereço de ativação.
4. No celular ou computador, acerte `https://www.google.com/device`, digite o código exibido e autorize o acesso.
5. O console detectará a autorização automaticamente e salvará as credenciais no SD.

---

## Utilização e Controles

### Tela Principal

| Comando | Ação |
| :--- | :--- |
| **D-Pad / Analógico Cima/Baixo** | Navega entre os cards de ação |
| **Botão (A)** | Confirma a ação selecionada |
| **Botão (-)** ou **Botão (Y)** | Abre a tela de Configurações |
| **Botão (+)** | Encerra o aplicativo |

### Ações de Rotina:
- **Backup Completo:** Identifica todos os saves do usuário ativo e envia para a nuvem.
- **Restaurar da Nuvem:** Baixa e atualiza saves locais caso a versão remota seja mais recente.
- **Gerenciar por Jogo:** Abre a lista detalhada por título com miniaturas e histórico.
- **Vincular Conta Google:** Inicia o fluxo de pareamento OAuth Device Flow.

### Tela de Seleção por Jogo

| Comando | Ação |
| :--- | :--- |
| **D-Pad / Analógico Cima/Baixo** | Percorre a lista de jogos instalados |
| **Botão (A)** | Sincroniza o save do jogo selecionado |
| **Botão (X)** | Abre o histórico de versões e snapshots na nuvem |
| **Botão (B)** | Retorna para a Tela Principal |

### Modal de Operação em Andamento
- **Botão (B):** Cancela a transferência imediatamente sem corromper arquivos.

---

## Estrutura de Armazenamento no Google Drive

Os backups são organizados na pasta raiz configurada (padrão: `Switch Saves/`):

```text
Google Drive/
 └── Switch Saves/
      ├── save_01006F8002326000.zip                  # Snapshot raiz do título (atualizado via PATCH)
      └── 01006F8002326000/                         # Diretório do Title ID
           └── 0123456789ABCDEFFEDCBA9876543210/     # Diretório do Account UID
                ├── 01006F8002326000_..._1762164930_1A2B3C4D.zip # Versão histórica 1
                └── 01006F8002326000_..._1762165930_9F8E7D6C.zip # Versão histórica 2
```

---

## Compilação do Código-Fonte

### Pré-requisitos

Instale o ambiente [devkitPro](https://devkitpro.org/) com a toolchain do Nintendo Switch:

```bash
dkp-pacman -S switch-dev switch-curl switch-mbedtls switch-zlib
```

### Compilar a Aplicação (.NRO)

```bash
# Clone o repositório recursivamente
git clone https://github.com/Lacas0010/switch-gdrive-cloud-save.git
cd switch-gdrive-cloud-save

# Compila o binário gdrive-sync.nro
make -C gui
```

O arquivo compilado estará disponível em `gui/gdrive-sync.nro`.

### Executar a Suíte de Testes Automatizada

```bash
# Instala as dependências Python para os testes de integração
pip install -r tests/requirements-test.txt

# Executa todos os testes unitários (C++/Catch2) e testes de integração (Pytest)
./tests/run_tests.sh
```

---

## Licença e Créditos

Este projeto é distribuído sob os termos da licença **GPL-3.0**. Consulte o arquivo `LICENSE` para detalhes.

### Bibliotecas de Terceiros:
- [libnx](https://github.com/switchbrew/libnx) - Biblioteca base para desenvolvimento Nintendo Switch Homebrew.
- [libcurl](https://curl.se/) & [mbedTLS](https://github.com/Mbed-TLS/mbedtls) - Comunicação HTTP segura via TLS 1.2/1.3.
- [zlib](https://zlib.net/) - Algoritmo de compressão e descompressão Deflate.
- [stb_truetype](https://github.com/nothings/stb) - Rasterização e medição vetorial de fontes.
- [stb_image](https://github.com/nothings/stb) - Decodificação de ícones JPEG em memória.
