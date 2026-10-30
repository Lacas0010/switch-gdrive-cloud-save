# ☁️ Nintendo Switch - Google Drive Cloud Save Suite

<p align="center">
  <img src="gui/icon.jpg" alt="Google Drive Cloud Save Icon" width="180" height="180" style="border-radius: 20px; box-shadow: 0 4px 16px rgba(0,0,0,0.4);" />
</p>

<p align="center">
  <b>Backup automático, transparente e seguro dos seus saves de jogos do Nintendo Switch direto para o Google Drive.</b>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Plataforma-Nintendo%20Switch-E60012?style=for-the-badge&logo=nintendo-switch&logoColor=white" alt="Nintendo Switch" />
  <img src="https://img.shields.io/badge/CFW-Atmosph%C3%A8re%201.7%2B-00D2FF?style=for-the-badge" alt="Atmosphere" />
  <img src="https://img.shields.io/badge/Google-Drive%20API%20v3-34A853?style=for-the-badge&logo=googledrive&logoColor=white" alt="Google Drive" />
  <img src="https://img.shields.io/badge/Interface-Nativa%20720p%2060FPS-FFAE00?style=for-the-badge" alt="Native 60FPS UI" />
</p>

---

## 📌 Sumário
1. [Visão Geral](#-visão-geral)
2. [Componentes da Suite](#-componentes-da-suite)
3. [Como Funciona (Arquitetura)](#-como-funciona-arquitetura)
4. [Instalação Rápida no Cartão SD](#-instalação-rápida-no-cartão-sd)
5. [Configuração das Credenciais do Google Drive](#-configuração-das-credenciais-do-google-drive)
6. [Vinculando sua Conta pelo Switch](#-vinculando-sua-conta-pelo-switch)
7. [Controles e Navegação do Aplicativo](#-controles-e-navegação-do-aplicativo)
8. [Certificados SSL e Criptografia](#-certificados-ssl-e-criptografia)
9. [Perguntas Frequentes e Resolução de Problemas](#-perguntas-frequentes-e-resolução-de-problemas)
10. [Como Compilar do Código-Fonte](#-como-compilar-do-código-fonte)

---

## 🌟 Visão Geral

O **Google Drive Cloud Save Suite** transforma o seu Nintendo Switch desbloqueado em uma central moderna de backup em nuvem, similar aos serviços premium da Nintendo Switch Online, Steam Cloud ou Xbox Cloud, mas utilizando a **sua própria conta pessoal do Google Drive** com armazenamento gratuito ou pago.

### Principais Destaques:
* 🔄 **Totalmente Invisível e Automático:** O daemon roda em segundo plano. Você joga normalmente; quando fecha o jogo pelo menu Home, o save é sincronizado sem que você precise abrir nenhum menu.
* ⚡ **Zero Lag nos Jogos:** O sysmodule utiliza um pool fixo de apenas 2MB de memória RAM no `.bss` de sistema, garantindo **0% de impacto** no desempenho mesmo em jogos pesados como *Zelda: Tears of the Kingdom* ou *Witcher 3*.
* 🗂️ **Zero Bagunça no Google Drive:** Cria automaticamente a pasta `Switch Saves` e gerencia atualizações inteligentes via requisições `PATCH` (sobrescrevendo o save anterior) e `POST` (criando novos saves), sem duplicar arquivos nem lotar sua lixeira.
* 🔐 **Detecção por Hash CRC32:** O sistema lê os arquivos do save e só inicia o upload se você realmente avançou no jogo e alterou o progresso.
* 🎨 **Interface Gráfica Nativa de Alta Qualidade:** Aplicativo Homebrew (.NRO) moderno renderizado a 720p nativo @ 60 FPS, com visual escuro, destaques em Neon Cyan/Red (Joy-Con), modais de autorização e suporte ao teclado oficial do Switch.

---

## 📦 Componentes da Suite

O projeto é dividido em dois módulos sincronizados:

| Componente | Tipo | Caminho no SD | Descrição |
| :--- | :--- | :--- | :--- |
| **Daemon CloudSave** | Sysmodule (`exefs.nsp`) | `sdmc:/atmosphere/contents/4200000000000010/` | Fica ativo em segundo plano no Switch monitorando o encerramento dos jogos via serviços do sistema (`pm:dmnt` / `pm:info`). |
| **Google Drive Sync** | Aplicativo (`.nro`) | `sdmc:/switch/gdrive-sync/gdrive-sync.nro` | Interface visual completa aberta pelo Homebrew Menu para configurar credenciais, parear conta, testar conexão e gerenciar o daemon. |
| **Configurações** | Arquivo JSON | `sdmc:/config/gdrive-sync/config.json` | Armazena tokens OAuth 2.0 (access_token, refresh_token), client_id, client_secret e o ID da pasta do Drive. |

---

## ⚙️ Como Funciona (Arquitetura)

```text
  [ JOGADOR ] ──> Joga normalmente (ex: Zelda BOTW)
       │
       ▼ Pressiona HOME e fecha o jogo (X -> Fechar)
  [ KERNEL HORIZON ] ──> Emite evento de término de processo (ExitProcess)
       │
       ▼
  [ SYSMODULE EM BACKGROUND (0x4200000000000010) ]
       │ 1. Intercepta o fechamento via 'pm:dmnt'
       │ 2. Descobre qual jogo foi encerrado (Title ID) e qual usuário estava jogando ('acc:u0')
       │ 3. Monta o sistema de arquivos do save em tempo real via 'fs-srv'
       │ 4. Calcula o CRC32 acumulado de todos os arquivos de save
       │
   [ Houve alteração no save? ]
       ├── NÃO ──> Não faz nada e volta a dormir (economiza bateria e internet)
       └── SIM ──> Inicia pipeline de upload:
                     │ a. Compacta os dados do save em stream ZIP direto no SD
                     │ b. Verifica conexão Wi-Fi ativa via 'nifm:u'
                     │ c. Renova token OAuth com a Google caso tenha expirado
                     │ d. Verifica se o arquivo "[Title_ID]_[Game].zip" já existe na pasta
                     │      ├── Se SIM: executa PATCH (atualização in-place)
                     │      └── Se NÃO: executa POST (upload limpo)
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
           └── temp/                    <-- Diretório temporário para os arquivos ZIP
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
  "folder_name": "Switch Saves"
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

### 2. O backup funciona se o console estiver offline ou no modo avião?
Sim! O monitor de processos salva o estado de alteração do save. Quando você reconectar o Nintendo Switch à internet Wi-Fi e fechar o jogo, ele identificará a rede ativa via `nifm:u` e efetuará o envio imediatamente.

### 3. Posso restaurar meus saves do Google Drive de volta para o console?
Sim! Como todos os saves são armazenados no formato padrão `.zip` contendo os arquivos internos brutos, você pode baixar o ZIP diretamente do Google Drive no celular ou PC e restaurá-los usando ferramentas consagradas como o **JKSV** ou **Checkpoint**.

### 4. Como desativar temporariamente o backup automático?
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
