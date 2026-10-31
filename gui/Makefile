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
SOURCES         := source ../source
INCLUDES        := include source ../source

APP_TITLE       := Google Drive Sync
APP_AUTHOR      := Antigravity Dev
APP_VERSION     := 0.0.6

ARCH            := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS          := -g -Wall -O2 -ffunction-sections -fdata-sections \
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

# Compila os modulos da GUI e os modulos core do sistema
CPPFILES        := source/main.cpp source/OAuthManager.cpp source/DriveVersionManager.cpp source/RestoreManager.cpp ../source/GDriveClient.cpp ../source/SaveManager.cpp ../source/ZipHelper.cpp

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

main.o: $(TOPDIR)/source/main.cpp
	@echo "Compilando $(notdir $<)..."
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) $(INCLUDE) -c $< -o $@

OAuthManager.o: $(TOPDIR)/source/OAuthManager.cpp
	@echo "Compilando $(notdir $<)..."
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) $(INCLUDE) -c $< -o $@

DriveVersionManager.o: $(TOPDIR)/source/DriveVersionManager.cpp
	@echo "Compilando $(notdir $<)..."
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) $(INCLUDE) -c $< -o $@

RestoreManager.o: $(TOPDIR)/source/RestoreManager.cpp
	@echo "Compilando $(notdir $<)..."
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) $(INCLUDE) -c $< -o $@

GDriveClient.o: $(TOPDIR)/../source/GDriveClient.cpp
	@echo "Compilando $(notdir $<)..."
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) $(INCLUDE) -c $< -o $@

SaveManager.o: $(TOPDIR)/../source/SaveManager.cpp
	@echo "Compilando $(notdir $<)..."
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) $(INCLUDE) -c $< -o $@

ZipHelper.o: $(TOPDIR)/../source/ZipHelper.cpp
	@echo "Compilando $(notdir $<)..."
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) $(INCLUDE) -c $< -o $@

%.o: $(TOPDIR)/source/%.cpp
	@echo "Compilando $(notdir $<)..."
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) $(INCLUDE) -c $< -o $@

%.o: $(TOPDIR)/../source/%.cpp
	@echo "Compilando $(notdir $<)..."
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) $(INCLUDE) -c $< -o $@

-include $(DEPSDIR)/*.d

endif
