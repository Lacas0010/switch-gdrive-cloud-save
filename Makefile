#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Por favor, defina a variavel de ambiente DEVKITPRO")
endif

include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# Configuracoes do Sysmodule
#---------------------------------------------------------------------------------
TARGET          := switch-cloud-save
TITLE_ID        := 4200000000000010
BUILD           := build
SOURCES         := source
INCLUDES        := include source

ARCH            := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS          := -g -Wall -O2 -ffunction-sections -fdata-sections \
                   $(ARCH) $(DEFINES)

CXXFLAGS        := $(CFLAGS) -std=gnu++20 -fno-rtti -fno-exceptions

ASFLAGS         := -g $(ARCH)

# Bibliotecas essenciais para networking e compactacao em sysmodule
LIBS            := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lnx

PORTLIBS        ?= $(DEVKITPRO)/portlibs/switch
LIBDIRS         := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES          := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES        := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES          := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export OFILES   := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all dist gui-app

all: $(BUILD) gui-app

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

gui-app:
	@echo "--- Compilando Aplicativo GUI (.NRO) ---"
	@$(MAKE) --no-print-directory -C gui -f Makefile

dist: all
	@echo "Montando estrutura completa para o Cartao SD..."
	@mkdir -p dist/switch/gdrive-sync
	@cp gui/gdrive-sync.nro dist/switch/gdrive-sync/gdrive-sync.nro
	@mkdir -p dist/config/gdrive-sync/temp
	@cp $(TOPDIR)/config.example.json dist/config/gdrive-sync/config.example.json
	@echo "=========================================================="
	@echo " Pacote gerado com sucesso em ./dist/ :"
	@echo "   - dist/switch/gdrive-sync/gdrive-sync.nro (App Manual 100% Sob Demanda)"
	@echo "   - dist/config/gdrive-sync/ (Configuracoes)"
	@echo "=========================================================="

clean:
	@echo Limpando Sysmodule...
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nsp $(TARGET).nso $(TOPDIR)/exefs $(TOPDIR)/main.npdm dist
	@echo Limpando GUI...
	@$(MAKE) --no-print-directory -C gui clean

else

#---------------------------------------------------------------------------------
# Regras de compilacao internas do Sysmodule
#---------------------------------------------------------------------------------
.PHONY: all

all: $(OUTPUT).nsp

$(OUTPUT).nsp: $(OUTPUT).nso $(TOPDIR)/npdm.json
	@npdmtool $(TOPDIR)/npdm.json $(TOPDIR)/main.npdm
	@mkdir -p $(TOPDIR)/exefs
	@cp $(OUTPUT).nso $(TOPDIR)/exefs/main
	@cp $(TOPDIR)/main.npdm $(TOPDIR)/exefs/main.npdm
	@build_pfs0 $(TOPDIR)/exefs $(OUTPUT).nsp
	@echo "Sysmodule NSP gerado com sucesso: $(OUTPUT).nsp"

$(OUTPUT).nso: $(OUTPUT).elf
	@elf2nso $< $@

LDFLAGS         := -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -Wl,-Map,$(OUTPUT).map -Wl,--gc-sections

$(OUTPUT).elf: $(OFILES)
	@echo "Ligando $(notdir $@)..."
	@$(CXX) $(LDFLAGS) $^ $(LIBPATHS) $(LIBS) -o $@

%.o: %.cpp
	@echo "Compilando $(notdir $<)..."
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) $(INCLUDE) -c $< -o $@

%.o: %.c
	@echo "Compilando $(notdir $<)..."
	@$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d $(CFLAGS) $(INCLUDE) -c $< -o $@

-include $(DEPSDIR)/*.d

endif
