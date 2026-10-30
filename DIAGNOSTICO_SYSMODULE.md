# Diagnóstico Definitivo do Sysmodule (`0x809` e `0xE801`) — Google Drive Cloud Save

Este documento registra a investigação completa feita diretamente no código-fonte oficial do **Atmosphère** (`stratosphere/loader` e `libraries/libmesosphere`), **switch-tools** (`npdmtool.c`), **libnx** (`runtime/init.c` e `runtime/devices/fs_dev.c`) e sysmodules de referência (`switch-examples/templates/sysmodule`, `MissionControl`, `sys-clk`).

---

## 1. Causa Raiz do Erro `0x809` (`2009-0004` — `ldr::ResultInvalidMeta`)

No código-fonte do Atmosphère em `stratosphere/loader/source/ldr_meta.cpp` (linhas 103–111):

```cpp
Result ValidateAcid(const Acid *acid, size_t size) {
    /* Validate magic. */
    R_UNLESS(acid->magic == Acid::Magic, ldr::ResultInvalidMeta());

    /* Validate that the acid is for production if not development. */
    if (!IsDevelopmentForAcidProductionCheck()) {
        R_UNLESS((acid->flags & Acid::AcidFlag_Production) != 0, ldr::ResultInvalidMeta());
    }
```

E no código-fonte do `npdmtool.c` (linhas 397–402):
```c
int is_retail;
if (!cJSON_GetBoolean(npdm_json, "is_retail", &is_retail)) { ... }
acid->Flags |= is_retail;
```

- **Explicação:** Em qualquer Nintendo Switch de varejo/produção (`!IsDevelopmentForAcidProductionCheck()`), o Loader (`ldr`) exige obrigatoriamente que o bit 0 de `acid->flags` (`AcidFlag_Production`) seja `1`.
- Quando `"is_retail": false` foi colocado no `npdm.json`, o bit `AcidFlag_Production` ficou `0` (`acid->flags = 0x8`).
- O Loader do Atmosphère barra imediatamente o arquivo `main.npdm` na linha 109 de `ldr_meta.cpp` retornando **`ldr::ResultInvalidMeta()` (`0x809`)** antes mesmo de chegar ao Kernel!
- **Solução Obrigatória:** `"is_retail": true` no `npdm.json`.

---

## 2. Causa Raiz do Erro `0xE801` (`2001-0116` — `svc::ResultInvalidCombination`)

A hipótese anterior (de que `"is_retail": true` conflitaria com `"pool_partition": 2`) estava **incorreta**: no Atmosphère (`ldr_process_creation.cpp:174`), `IsApplication(meta)` não lê `is_retail`, e sim o descritor `ApplicationType` de `kernel_capabilities`. Todos os sysmodules oficiais de homebrew (`sys-clk`, `MissionControl`, `switch-examples`) usam `"is_retail": true` junto com `"pool_partition": 2`.

O verdadeiro motivo de `svcCreateProcess` retornar `0xE801` (`svc::ResultInvalidCombination`) quando `"is_retail": true` está em **`libraries/libmesosphere/source/kern_k_capabilities.cpp` (linhas 48–63)** e **`libraries/libmesosphere/source/kern_k_process.cpp` (linhas 181–187)**:

1. Quando o Loader chama `svc::CreateProcess(...)`, o Kernel (Mesosphère) executa `KCapabilities::Initialize(svc::KUserPointer<const u32 *> user_caps, ...)` (linha 48 de `kern_k_capabilities.cpp`):
   ```cpp
   m_intended_kernel_version = {0};
   ```
   Para processos criados via `svcCreateProcess`, `m_intended_kernel_version` começa zerado (`0`).
2. Em seguida, o Kernel lê os descritores de `kernel_capabilities` do `main.npdm`.
   Como o nosso `npdm.json` **não possuía o descritor `"min_kernel_version"`**, `m_intended_kernel_version` continuou sendo **`0`**!
3. Logo depois, em `KProcess::Initialize(const ams::svc::CreateProcessParameter &params)` (linha 183 de `kern_k_process.cpp`), o Kernel executa:
   ```cpp
   /* Validate that the intended kernel version is high enough for us to support. */
   R_UNLESS(m_capabilities.GetIntendedKernelVersion() >= ams::svc::RequiredKernelVersion, svc::ResultInvalidCombination());

   /* Validate that the intended kernel version isn't too high for us to support. */
   R_UNLESS(m_capabilities.GetIntendedKernelVersion() <= ams::svc::SupportedKernelVersion, svc::ResultInvalidCombination());
   ```
   Como `0 < RequiredKernelVersion` (`0x0030` = Kernel 3.0), a checagem falha na linha 183 e o Kernel retorna exatamente **`svc::ResultInvalidCombination()` (`0xE801`)**!

### Outros problemas encontrados no `npdm.json`:
- **Falta de `"min_kernel_version": "0x0030"`** em `kernel_capabilities` (causa direta do `0xE801`).
- **Falta de `"handle_table_size": 1023`** em `kernel_capabilities` (sem ele, `m_handle_table_size` fica `0`).
- **Falta de `"debug_flags"`** (`allow_debug: false`, `force_debug: true`, `force_debug_prod: false`) exigido pelo template canônico do `switch-examples` e firmwares 19.0.0+.
- **IDs de syscalls incorretos no `npdm.json` antigo:**
  - `svcGetProcessId` estava mapeado como `"0x29"` (que na verdade é `svcGetInfo`), enquanto o ID real de `svcGetProcessId` é `"0x24"`.
  - `svcBreak` estava como `"0x2c"` (`svcMapPhysicalMemory`), quando o real é `"0x26"`.
  - `svcOutputDebugString` estava como `"0x2d"` (`svcUnmapPhysicalMemory`), quando o real é `"0x27"`.
  - Faltavam syscalls essenciais da `libnx` moderna: `svcSetMemoryPermission` (`0x02`), `svcGetThreadId` (`0x25`), `svcReturnFromException` (`0x28`), `svcWaitForAddress` (`0x34`), `svcSignalToAddress` (`0x35`), `svcSynchronizePreemptionState` (`0x36`).

---

## 3. Causa Raiz do Sysmodule Fechar Sozinho no Boot (`source/main.cpp`)

Mesmo após o Kernel criar o processo, havia dois problemas fatais na inicialização em `source/main.cpp`:

1. **Ausência de `void __appInit(void)` e `void __appExit(void)` sobrescritos:**
   - Em `libnx` (`nx/source/runtime/init.c`), antes de `main()` ser chamada, a `crt0` executa `__libnx_init()`, que chama `__appInit()`.
   - Como `source/main.cpp` não declarou `extern "C" void __appInit(void)`, a `libnx` executava o `__appInit()` padrão (de aplicativos comuns), que já chama `smInitialize()`, `appletInitialize()`, `timeInitialize()`, `fsInitialize()` e **`fsdevMountSdmc()`**.
   - Quando `main()` começava em `source/main.cpp` (linha 72), ela chamava `fsdevMountSdmc()` **pela segunda vez**!
   - No código da `libnx` (`fs_dev.c`, linhas 312 e 449):
     ```c
     if(fsdevFindDevice(name)) // Device is already mounted with the same name ("sdmc").
       goto _fail; // returns -1
     ```
     E `fsdevMountSdmc()` converte `ret == -1` em `MAKERESULT(Module_Libnx, LibnxError_OutOfMemory)`.
   - Na linha 73 de `source/main.cpp`:
     ```cpp
     rc = fsdevMountSdmc();
     if (R_FAILED(rc)) { fsExit(); smExit(); return 1; }
     ```
     Isso fazia `R_FAILED(rc)` dar `true` sempre e o sysmodule **encerrar imediatamente com `return 1`** no primeiro milissegundo de execução!

2. **Inicialização precoce no `boot2.flag` antes do Home Menu (`qlaunch`) e rede estarem prontos:**
   - Quando carregado no boot via `boot2.flag`, o sysmodule inicia antes dos serviços de rede (`nifm` / `bsd` sockets) e do Home Menu (`qlaunch`, Title ID `0x0100000000001000`) estarem prontos.
   - Se `nifmInitialize` ou `socketInitialize` falharem no boot precoce e `main()` der `return 1`, o daemon morre no boot.
   - Assim como no `sys-clk`, o correto é:
     - Definir `__appInit()` e `__appExit()` canônicos de sysmodule (inicializando `sm`, `setsys` para `hosversionSet`, `time`, `fs` e `fsdevMountSdmc()` uma única vez).
     - Em `main()`, aguardar o `qlaunch` (`0x0100000000001000`) ou inicializar `pmdmnt`, `pminfo`, `nifm` e `socket` com retry resiliente sem matar o processo caso a pilha de rede ainda esteja subindo.

---

## 4. Resumo das Correções Aplicadas

1. **[`npdm.json`](file:///c:/Users/lucas/OneDrive/Documents/nuuvem%20switch%20desbloqueado/npdm.json):**
   - `"is_retail": true` (resolve `0x809` em `ldr_meta.cpp:109`).
   - Adicionado `{"type": "min_kernel_version", "value": "0x0030"}` (resolve `0xE801` em `kern_k_process.cpp:183`).
   - Adicionado `{"type": "handle_table_size", "value": 512}`.
   - Adicionado `{"type": "debug_flags", "value": {"allow_debug": false, "force_debug": true, "force_debug_prod": false}}`.
   - Tabela completa e exata de `syscalls` do template oficial `switchbrew/switch-examples/templates/sysmodule/sysmodule.json`.
   - `lowest_cpu_id: 3`, `highest_cpu_id: 3`, `default_cpu_id: 3`, `main_thread_priority: 44`.

2. **[`source/main.cpp`](file:///c:/Users/lucas/OneDrive/Documents/nuuvem%20switch%20desbloqueado/source/main.cpp):**
   - Implementados `extern "C" void __appInit(void)` e `extern "C" void __appExit(void)` seguindo o template oficial do `switchbrew/switch-examples` e `sys-clk` (sem chamar `fsdevMountSdmc()` duas vezes).
   - Adicionado log de diagnóstico em arquivo (`sdmc:/config/gdrive-sync/sysmodule.log`) para rastrear cada etapa do boot e funcionamento em background.
   - Inicialização resiliente dos serviços (`pmdmnt`, `pminfo`, `nifm`, `socket`) com loop de espera pelo sistema no boot.
