# ☁️ Nintendo Switch - Google Drive Cloud Save Suite

<p align="center">
  <img src="gui/icon.jpg" alt="Google Drive Cloud Save Icon" width="180" height="180" style="border-radius: 20px; box-shadow: 0 4px 16px rgba(0,0,0,0.4);" />
</p>

<p align="center">
  <b>Backup automático, transparente e seguro dos seus saves de jogos do Nintendo Switch direto para o Google Drive.</b>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Vers%C3%A3o-v0.0.2-34A853?style=for-the-badge&logo=github&logoColor=white" alt="Versão v0.0.2" />
  <img src="https://img.shields.io/badge/Plataforma-Nintendo%20Switch-E60012?style=for-the-badge&logo=nintendo-switch&logoColor=white" alt="Nintendo Switch" />
  <img src="https://img.shields.io/badge/CFW-Atmosph%C3%A8re%201.7%2B-00D2FF?style=for-the-badge" alt="Atmosphere" />
  <img src="https://img.shields.io/badge/Google-Drive%20API%20v3-4285F4?style=for-the-badge&logo=googledrive&logoColor=white" alt="Google Drive" />
  <img src="https://img.shields.io/badge/Interface-Nativa%20720p%2060FPS-FFAE00?style=for-the-badge" alt="Native 60FPS UI" />
</p>

---

## 📌 Sumário
1. [Visão Geral](#-visão-geral)
2. [Novidades da Versão 0.0.2](#-novidades-da-versão-002)
3. [Componentes da Suite](#-componentes-da-suite)
4. [Como Funciona (Arquitetura)](#-como-funciona-arquitetura)
5. [Instalação Rápida no Cartão SD](#-instalação-rápida-no-cartão-sd)
6. [Configuração das Credenciais do Google Drive](#-configuração-das-credenciais-do-google-drive)
7. [Vinculando sua Conta pelo Switch](#-vinculando-sua-conta-pelo-switch)
8. [Controles e Navegação do Aplicativo](#-controles-e-navegação-do-aplicativo)
9. [Certificados SSL e Criptografia](#-certificados-ssl-e-criptografia)
10. [Perguntas Frequentes e Resolução de Problemas](#-perguntas-frequentes-e-resolução-de-problemas)
11. [Como Compilar do Código-Fonte](#-como-compilar-do-código-fonte)

---

## 🌟 Visão Geral

O **Google Drive Cloud Save Suite** transforma o seu Nintendo Switch desbloqueado em uma central moderna de backup em nuvem, similar aos serviços premium da Nintendo Switch Online, Steam Cloud ou Xbox Cloud, mas utilizando a **sua própria conta pessoal do Google Drive** com armazenamento gratuito ou pago.

### Principais Destaques:
* 🔄 **Totalmente Invisível e Automático:** O daemon roda em segundo plano. Você joga normalmente; quando fecha o jogo pelo menu Home, o save é sincronizado sem que você precise abrir nenhum menu.
* ⚡ **Zero Lag nos Jogos:** O sysmodule utiliza um pool fixo de apenas 2MB de memória RAM no `.bss` de sistema, garantindo **0% de impacto** no desempenho mesmo em jogos pesados como *Zelda: Tears of the Kingdom* ou *The Witcher 3*.
* 🗂️ **Zero Bagunça no Google Drive:** Cria automaticamente a pasta `Switch Saves` e gerencia atualizações inteligentes via requisições `PATCH` (sobrescrevendo o save anterior) e `POST` (criando novos saves), sem duplicar arquivos nem lotar sua lixeira.
* 🔐 **Detecção por Hash CRC32:** O sistema lê os arquivos do save e só inicia o upload se você realmente avançou no jogo e alterou o progresso.
* 📦 **Upload Manual em Lote ou Seletivo:** Além do backup automático, a interface gráfica permite enviar todos os saves de uma só vez ou selecionar individualmente quais jogos você deseja sincronizar, com os nomes reais dos jogos descobertos diretamente dos metadados oficiais do Switch.
* 📶 **Resiliência a Comunicação Local (LDN / Y-Comm):** Suporte nativo e automático a jogos como *Pokémon Sword/Shield* e *Animal Crossing*, aguardando a reconexão com a rede Wi-Fi residencial antes do upload.
* 💾 **Fila Offline com Auto-Recuperação:** Se o console estiver offline no momento do fechamento, o save é enfileirado no cartão SD e enviado em segundo plano assim que a conexão retornar, inclusive sobrevivendo a reinicializações.
* 🎨 **Interface Gráfica Nativa de Alta Qualidade:** Aplicativo Homebrew (.NRO) moderno renderizado a 720p nativo @ 60 FPS, com visual escuro, destaques em Neon Cyan/Red (Joy-Con), modais de autorização e suporte ao teclado oficial do Switch.

---

## 🚀 Novidades da Versão 0.0.2

Esta versão traz melhorias substanciais de estabilidade, novas opções na interface gráfica e tratamento especializado de redes sem fio:

1. **Subir Todos os Saves Agora (Upload em Lote):**
   - Nova opção no menu principal da GUI que varre todos os saves do usuário ativo e realiza o upload sequencial com barra de progresso em tempo real e tela de resumo com contagem de sucessos/falhas.
   - Força a criação do arquivo ZIP mesmo se o hash CRC32 for idêntico ao do último envio, permitindo forçar um backup completo a qualquer momento.

2. **Selecionar Jogos para Sincronizar (Lista Interativa com Nomes Reais):**
   - Visualização com rolagem (scroll) de todos os jogos instalados que possuem saves.
   - Resolução automática dos nomes reais dos jogos via biblioteca de metadados oficiais (`ns` / `nacp`), com fallback inteligente para o Title ID caso os metadados não existam.
   - Controles ágeis: seleção individual (`A`), marcar/desmarcar todos (`Y`), envio imediato da seleção (`+`) e salvar como filtro permanente (`X`).

3. **Modo Whitelist Persistente no Sysmodule:**
   - Possibilidade de salvar a lista de jogos monitorados no `config.json` (`"sync_mode": "whitelist"` e `"tracked_titles"`). Quando ativo, o daemon em background ignora jogos não autorizados pelo usuário.

4. **Tolerância a Jogos com Comunicação Local (LDN / Y-Comm):**
   - Jogos como *Pokémon Sword/Shield*, *Pokémon Scarlet/Violet* e *Animal Crossing* ativam o chip sem fio em modo ad-hoc local para recursos multiplayer, desconectando o Switch do roteador Wi-Fi (e de fones Bluetooth).
   - O daemon agora detecta quando a rede está desconectada e aguarda ativamente até 15 segundos pela reconexão automática com a rede residencial antes de tentar o upload.

5. **Fila Offline de Uploads Pendentes (`s_pendingSaves` & Auto-Recuperação):**
   - Se a internet não estiver disponível ao fechar o jogo, o save compactado é mantido no SD em `sdmc:/config/gdrive-sync/temp/pending_[TitleID]_[Timestamp].zip`.
   - O daemon tenta o envio periódico em segundo plano e, ao ligar o Switch (`boot2`), recupera e transmite automaticamente quaisquer saves pendentes em disco.

6. **Montagem Resiliente do Cartão SD no Boot:**
   - Adicionado loop de espera e retry de até 15 segundos para `fsdevMountSdmc()`, prevenindo falhas de inicialização em cartões SD mais lentos ou no arranque a frio (`boot2.flag`).

7. **Normalização de Title IDs de Atualizações (Patches):**
   - Mapeamento de Title IDs que executam sob a flag de patch/update (`titleId & ~0x800ULL`), garantindo a localização exata do save original de qualquer jogo atualizado.

8. **Upload por Streaming cURL:**
   - Implementação de `CURLOPT_READFUNCTION` com streaming contínuo de arquivo (`fopen`/`fread`), evitando sobrecarga de buffer na memória RAM e validando detalhadamente códigos de resposta HTTP.

9. **Atualizar do Drive (Restauração Inteligente / Download se Nuvem for Mais Recente):**
   - Novo botão dedicado no menu principal da interface Homebrew (.NRO).
   - Compara o timestamp UTC (`modifiedTime`) de cada save na pasta do Google Drive com o timestamp local do save no console (`timestamps.json` ou data dos arquivos internos).
   - Se `Timestamp_Drive > Timestamp_Local`: realiza o download do arquivo ZIP em chunks com barra de progresso, cria automaticamente uma cópia de segurança do save local atual em `sdmc:/config/gdrive-sync/backups/<TitleID>_backup.zip`, descompacta diretamente na partição `save:/` e executa `fsdevCommitDevice("save")` para persistência segura na memória interna/SD.
   - Saves onde a versão local já é igual ou mais recente são ignorados automaticamente, com relatório detalhado exibido em um modal ao final da operação.

---

## 📦 Componentes da Suite

| Componente | Tipo | Caminho no SD | Descrição |
| :--- | :--- | :--- | :--- |
| **Daemon CloudSave** | Sysmodule (`exefs.nsp`) | `sdmc:/atmosphere/contents/4200000000000010/` | Fica ativo em segundo plano no Switch monitorando o encerramento dos jogos via serviços do sistema (`pm:dmnt` / `pm:info`). |
| **Google Drive Sync** | Aplicativo (`.nro`) | `sdmc:/switch/gdrive-sync/gdrive-sync.nro` | Interface visual completa aberta pelo Homebrew Menu para configurar credenciais, parear conta, testar conexão, listar e subir saves manualmente. |
| **Configurações** | Arquivo JSON | `sdmc:/config/gdrive-sync/config.json` | Armazena tokens OAuth 2.0 (access_token, refresh_token), client_id, client_secret, folder_id, modo de sincronização e títulos autorizados. |
| **Logs de Diagnóstico** | Arquivo Texto | `sdmc:/config/gdrive-sync/gdrive_sync.log` | Registra detalhes operacionais, códigos de status HTTP e eventuais erros de rede. |

---

## ⚙️ Como Funciona (Arquitetura)

```text
  [ JOGADOR ] ──> Joga normalmente (ex: Zelda BOTW, Pokémon Sword)
       │
       ▼ Pressiona HOME e fecha o jogo (X -> Fechar)
  [ KERNEL HORIZON ] ──> Emite evento de término de processo (ExitProcess)
       │
       ▼
  [ SYSMODULE EM BACKGROUND (0x4200000000000010) ]
       │ 1. Intercepta o fechamento via 'pm:dmnt'
       │ 2. Normaliza o Title ID (suporte a patches/updates) e usuário ativo ('acc:u0')
       │ 3. Filtra pelo modo de sincronização (All vs Whitelist de jogos rastreados)
       │ 4. Monta o save via 'fs-srv' e calcula hash CRC32 acumulado
       │
   [ Houve alteração no save? ]
       ├── NÃO ──> Não faz nada e volta a dormir (economiza bateria e internet)
       └── SIM ──> Inicia pipeline de upload:
                     │ a. Compacta os dados em ZIP direto no SD
                     │ b. Verifica conexão Wi-Fi ativa ('nifm:u'):
                     │      ├── Se em LDN/ad-hoc (ex: Pokémon): aguarda reconexão do roteador (até 15s)
                     │      └── Se offline: guarda em fila pendente em disco ('pending_*.zip')
                     │ c. Renova token OAuth com a Google caso necessário
                     │ d. Upload via streaming HTTP (PATCH para atualizar, POST para novo save)
                     │ e. Limpa o arquivo temporário do SD
                     └── Save seguro na nuvem! ☁️
```

---

## 🚀 Instalação Rápida no Cartão SD

Se você está com o Nintendo Switch conectado ao computador (via DBI em modo MTP, leitor de cartão ou FTP), a estrutura de pastas no cartão SD deve ficar exatamente assim:

```text
sdmc:/ (Raiz do Cartão SD)
 ├── atmosphere/
 │    └── contents/
 │         └── 4200000000000010/
 │              ├── exefs.nsp           <-- Executável do Daemon em segundo plano
 │              ├── toolbox.json        <-- Informações para Tesla Overlay / Am-Toolbox
 │              └── flags/
 │                   └── boot2.flag     <-- Garante que o Daemon ligue junto com o console
 ├── switch/
 │    └── gdrive-sync/
 │         └── gdrive-sync.nro          <-- Aplicativo gráfico com ícone na interface
 └── config/
      └── gdrive-sync/
           ├── config.json              <-- Suas chaves OAuth (client_id, secret, tokens)
           ├── cacert.pem               <-- Certificados SSL de validação HTTPS
           ├── gdrive_sync.log          <-- Log de operações e diagnósticos
           └── temp/                    <-- Diretório temporário para os arquivos ZIP e fila pendente
```

> [!TIP]
> **Inicialização Automática:** Com a presença do arquivo `boot2.flag`, o Atmosphère inicializa o serviço de backup automaticamente em todo reboot do Switch. Você não precisa fazer nada manualmente!

---

## 🔑 Configuração das Credenciais do Google Drive

Para que o aplicativo possa se conectar com a sua conta Google, é necessário criar um identificador OAuth no Google Cloud Console (processo 100% gratuito e leva cerca de 3 minutos):

### Passo 1: Criar o Projeto
1. Acesse o [Google Cloud Console](https://console.cloud.google.com/) com a sua conta Google.
2. No menu superior, clique em **Selecionar Projeto** e depois em **Novo Projeto**.
3. Dê um nome como `Switch Cloud Save` e clique em **Criar**.

### Passo 2: Ativar a API do Google Drive
1. No menu lateral esquerdo, vá em **APIs e Serviços** > **Biblioteca**.
2. Na barra de pesquisa, digite `Google Drive API`.
3. Clique sobre o resultado e clique no botão azul **Ativar**.

### Passo 3: Configurar a Tela de Consentimento OAuth
1. No menu lateral, acesse **APIs e Serviços** > **Tela de permissão OAuth**.
2. Selecione o tipo de usuário **Externo** e clique em **Criar**.
3. Preencha apenas o básico:
   - **Nome do app:** `Switch Cloud Save`
   - **E-mail de suporte ao usuário:** seu próprio e-mail
   - **Dados de contato do desenvolvedor:** seu próprio e-mail
4. Clique em **Salvar e continuar**.
5. Na aba **Escopos**, clique em **Adicionar ou remover escopos**, marque o escopo:
   - `.../auth/drive.file` *(Ver, criar e editar apenas os arquivos usados por este app)*
   - Clique em **Atualizar** e depois em **Salvar e continuar**.
6. Na aba **Usuários de teste**, clique em **+ Add users** e digite o seu endereço de e-mail da conta Google. Clique em **Salvar e continuar**.

### Passo 4: Criar as Credenciais do Dispositivo
1. No menu lateral, clique em **Credenciais**.
2. Clique no botão superior **+ Criar credenciais** e selecione **ID do cliente OAuth**.
3. No campo **Tipo de aplicativo**, selecione:
   - 👉 **TVs e dispositivos com entrada limitada** *(TVs and Limited Input devices)*.
4. Dê o nome que preferir (ex: `Nintendo Switch`) e clique em **Criar**.
5. Uma janela exibirá o seu **ID do cliente** (`Client ID`) e a **Chave secreta do cliente** (`Client Secret`).

---

## 📱 Vinculando sua Conta pelo Switch

Você pode inserir o `client_id` e o `client_secret` de duas formas muito fáceis:

### Método A: Pelo computador no arquivo `config.json` (Mais Rápido)
Abra o arquivo `sdmc:/config/gdrive-sync/config.json` em qualquer editor de texto no seu PC e preencha:

```json
{
  "client_id": "SEU_CLIENT_ID_AQUI.apps.googleusercontent.com",
  "client_secret": "SEU_CLIENT_SECRET_AQUI",
  "refresh_token": "",
  "access_token": "",
  "token_expires_at": 0,
  "folder_id": "",
  "folder_name": "Switch Saves",
  "sync_mode": "all",
  "tracked_titles": []
}
```

### Método B: Pelo teclado virtual na tela do Switch
1. Inicie o Switch e abra o **Homebrew Menu** via **Title Override** (segure **[R]** ao abrir qualquer jogo instalado, como Zelda BOTW).
2. Abra o aplicativo **Google Drive Cloud Save**.
3. Pressione a opção de editar `Client ID` ou `Client Secret`: o teclado oficial do Horizon OS surgirá na tela para você digitar diretamente.

### Finalizando o Pareamento:
1. Dentro do aplicativo no Switch, selecione **`1. VINCULAR CONTA GOOGLE DRIVE`**.
2. Uma janela elegante se abrirá mostrando:
   - O endereço de acesso: `https://www.google.com/device`
   - Um código de 8 dígitos (exemplo: `ABCD-EFGH`).
3. No seu celular ou computador, abra o navegador, entre no link informado e digite o código de 8 dígitos.
4. Escolha sua conta Google e clique em **Permitir**.
5. **Pronto!** Em menos de 2 segundos a tela do Switch detectará a aprovação automaticamente, baixará os tokens com segurança e exibirá a confirmação de sucesso com o nome da sua pasta de saves.

---

## 🎮 Controles e Navegação do Aplicativo

### Menu Principal
| Botão | Ação |
| :---: | :--- |
| **D-Pad / Analógico Cima/Baixo** | Navega entre as opções e configurações. |
| **Botão (A)** | Seleciona a opção destacada ou abre o teclado para editar campos. |
| **Opção "Subir Todos os Saves Agora"** | Compacta e envia todos os saves do usuário ativo em lote com barra de progresso. |
| **Opção "Atualizar do Drive (Baixar se Nuvem for Mais Recente)"** | Compara timestamps e baixa/restaura da nuvem saves mais recentes com backup de segurança e commit. |
| **Opção "Selecionar Jogos para Sincronizar"** | Abre a lista interativa de jogos com nomes reais para marcar e sincronizar. |
| **Botão (X)** | Inicia ou encerra o Daemon em segundo plano. |
| **Botão (Y)** | Ativa ou desativa o **Auto-Boot** no início do console (`boot2.flag`). |
| **Botão (-)** | Abre a tela de Guia Rápido e Ajuda. |
| **Botão (+)** | Fecha o aplicativo com segurança. |

### Tela de Seleção de Jogos ("Selecionar Jogos para Sincronizar")
| Botão | Ação |
| :---: | :--- |
| **D-Pad / Analógico Cima/Baixo** | Navega pela lista de jogos (com rolagem/scroll suave). |
| **Botão (A)** | Marca ou desmarca o jogo (`[X]` / `[ ]`). |
| **Botão (Y)** | Marca todos ou desmarca todos os jogos de uma vez. |
| **Botão (X)** | Salva a seleção atual como padrão (`whitelist`) para o Sysmodule automático. |
| **Botão (+)** | Inicia imediatamente o upload apenas dos jogos selecionados. |
| **Botão (B)** | Cancela e volta para o menu principal. |

---

## 🔒 Certificados SSL e Criptografia

Todas as trocas de dados entre o console e os servidores da Google são protegidas por criptografia **TLS 1.2 / TLS 1.3** através da biblioteca `mbedTLS`. 

Para validação das Autoridades Certificadoras (CA) da Google, a suite utiliza o arquivo padrão da Mozilla `cacert.pem`. Ele deve ficar localizado em:
`sdmc:/config/gdrive-sync/cacert.pem`

---

## ❓ Perguntas Frequentes e Resolução de Problemas

### 1. Ao apertar [X] recebi o erro `0x809` ou `0xE801`. O que significa?
* **Causa:** O Homebrew Menu foi aberto em **Applet Mode** (abrindo o Álbum sem segurar R). Em modo applet, a memória RAM é estritamente limitada a 32MB e o Horizon OS bloqueia o serviço de controle de processos `pm:shell`.
* **Solução:** 
  1. Feche o Homebrew Menu.
  2. Mantenha o botão **[R]** pressionado no controle e clique para iniciar qualquer jogo instalado no seu menu principal (ex: Zelda BOTW, Mario Kart, etc.).
  3. Mantenha o botão **[R]** pressionado até o Homebrew Menu abrir com acesso total à memória (Title Override).
  4. Além disso, com a flag `boot2.flag` ativada, basta **reiniciar o console** uma vez para que o daemon carregue automaticamente no boot como serviço do sistema.

### 2. O backup funciona se o console estiver offline ou após jogos com comunicação local (como Pokémon)?
Sim! O sistema possui três camadas de tolerância:
1. **Reconexão LDN Automática:** Jogos que usam comunicação sem fio local (como *Pokémon Sword/Shield*, *Pokémon Scarlet/Violet*, *Animal Crossing*) colocam o chip Wi-Fi em modo ad-hoc e desconectam da rede de casa. Ao fechar esses jogos, o daemon aguarda ativamente até 15 segundos pela re-associação com o roteador.
2. **Fila Offline em Segundo Plano (`s_pendingSaves`):** Se a internet não estiver disponível no momento do fechamento, o arquivo `.zip` é enfileirado na pasta temporária (`sdmc:/config/gdrive-sync/temp/pending_*.zip`). Uma thread em segundo plano tenta enviá-lo assim que a conexão for reestabelecida.
3. **Auto-Recuperação no Boot:** Se o console for desligado antes do upload, no próximo arranque o daemon detecta e envia os saves pendentes automaticamente.

### 3. Por que um jogo não subiu logo após salvar e fechar?
O daemon calcula um hash CRC32 de todo o diretório do save e compara com o valor registrado em `sdmc:/config/gdrive-sync/state.json`. Se o save não sofreu modificações reais desde o último backup, o sistema pula o envio para poupar internet e bateria. Caso queira forçar o envio imediato, use a opção **"Subir Todos os Saves Agora"** ou **"Selecionar Jogos para Sincronizar"** na interface gráfica.

### 4. Posso restaurar meus saves do Google Drive de volta para o console?
Sim! Como todos os saves são armazenados no formato padrão `.zip` contendo os arquivos internos brutos, você pode baixar o ZIP diretamente do Google Drive no celular ou PC e restaurá-los usando ferramentas consagradas como o **JKSV** ou **Checkpoint**.

### 5. Como desativar temporariamente o backup automático?
Basta abrir o aplicativo gráfico no Switch e pressionar **[Y]** para desmarcar o Auto-Boot, ou apagar o arquivo `sdmc:/atmosphere/contents/4200000000000010/flags/boot2.flag`.

---

## 🛠️ Como Compilar do Código-Fonte

### Compilação Automatizada (Recomendada via GitHub Actions)
O repositório possui uma pipeline CI configurada em `.github/workflows/build.yml`. Ao fazer qualquer `push` para a branch `main`:
1. Um container oficial `devkitpro/devkita64:latest` compila o Sysmodule e o aplicativo GUI.
2. O pacote pronto para o SD é publicado automaticamente como artefato para download.

### Compilação Local com devkitPro
Caso tenha o ambiente devkitPro instalado localmente no seu computador:

```bash
# 1. Instalar as dependências do Switch:
dkp-pacman -S switch-dev switch-curl switch-mbedtls switch-zlib

# 2. Compilar tudo e montar o pacote SD:
make dist
```

O diretório `./dist` será gerado pronto para cópia direta no cartão SD do Switch.

---

<p align="center">
  Desenvolvido com carinho para a comunidade do Nintendo Switch. Tenha seus saves sempre seguros! 🛡️🕹️
</p>
