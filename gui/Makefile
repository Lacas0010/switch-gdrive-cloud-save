#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Por favor, defina a variavel de ambiente DEVKITPRO")
endif

include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# Configuracoes da Aplicacao GUI (.NRO)
#---------------------------------------------------------------------------------
TARGET          := gdrive-sync
BUILD           := build
SOURCES         := ../src ../src/core ../src/network ../src/storage ../src/ui ../src/utils
INCLUDES        := ../src ../src/core ../src/network ../src/storage ../src/ui ../src/utils

APP_TITLE       := Google Drive Sync
APP_AUTHOR      := Lacas0010
APP_VERSION     := 0.1.0

ARCH            := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS          := -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow -O2 -ffunction-sections -fdata-sections \
                   $(ARCH) $(DEFINES)

CXXFLAGS        := $(CFLAGS) -std=gnu++20 -fno-rtti -fno-exceptions

ASFLAGS         := -g $(ARCH)

# Bibliotecas necessarias
LIBS            := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lnx

PORTLIBS        ?= $(DEVKITPRO)/portlibs/switch
LIBDIRS         := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

# Compila todos os modulos da arquitetura modular src/
CPPFILES        := ../src/main.cpp \
                   ../src/core/sync_engine.cpp \
                   ../src/network/drive_client.cpp \
                   ../src/network/oauth_manager.cpp \
                   ../src/storage/save_manager.cpp \
                   ../src/storage/zip_util.cpp \
                   ../src/ui/texture_cache.cpp \
                   ../src/ui/screen_main.cpp \
                   ../src/ui/screen_select.cpp \
                   ../src/ui/ui_manager.cpp \
                   ../src/utils/log.cpp \
                   ../src/utils/time_util.cpp

export OFILES   := $(notdir $(CPPFILES:.cpp=.o))

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo Limpando aplicacao GUI...
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp

else

#---------------------------------------------------------------------------------
# Regras de compilacao internas
#---------------------------------------------------------------------------------
.PHONY: all

all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
	@if [ -f "$(TOPDIR)/icon.jpg" ]; then \
		elf2nro $< $@ --nacp=$(OUTPUT).nacp --icon=$(TOPDIR)/icon.jpg; \
	else \
		elf2nro $< $@ --nacp=$(OUTPUT).nacp; \
	fi
	@echo "NRO gerado com sucesso: $@"

$(OUTPUT).nacp:
	@nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

LDFLAGS         := -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -Wl,-Map,$(OUTPUT).map -Wl,--gc-sections

$(OUTPUT).elf: $(OFILES)
	@echo "Ligando $(notdir $@)..."
	@$(CXX) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

%.o: %.cpp
	@echo "Compilando $(notdir $<)..."
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) $(INCLUDE) -c $< -o $@

-include $(DEPSDIR)/*.d

endif
