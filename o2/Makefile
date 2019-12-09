ESP8266_SDK_ROOT=../esp-open-sdk

# Output directors to store intermediate compiled files
# relative to the project directory
BUILD_BASE	= build
FW_BASE		= roms

# base directory for the compiler


# # esptool.py path and port
ESPTOOL		= PATH=${PATH}:$(ESP8266_SDK_ROOT)/xtensa-lx106-elf/bin:$(ESP8266_SDK_ROOT)/esptool python2 $(ESP8266_SDK_ROOT)/xtensa-lx106-elf/bin/esptool.py
ESPPORT		= /dev/ttyUSB0
ESPTOOLBAUD	= 76800 # 460800 #76800
ESPTOOLOPTS	= -ff 40m -fm dio -fs 32m

# name for the target project
TARGET		= app

# which modules (subdirectories) of the project to include in compiling
MODULES		= src
EXTRA_INCDIR    = include

# libraries used in this project, mainly provided by the SDK
LIBS		= c gcc hal pp phy net80211 lwip_open wpa crypto driver main

# compiler flags using during compilation of source files
CFLAGS		= -DWIFI_PASSWORD=\""${WPA2_PASSPHRASE}"\" -DLWIP_OPEN_SRC -std=c99 -fwhole-program -Os -flto -Wpointer-arith -Wundef -Werror -Wl,-EL  -nostdlib -mlongcalls -mtext-section-literals  -fno-exceptions  -fno-builtin-printf -fno-jump-tables -mno-serialize-volatile  -fno-guess-branch-probability -freorder-blocks-and-partition -fno-cse-follow-jumps -fmerge-all-constants -fgcse-after-reload -fgcse-las -fconserve-stack -ftree-pre -ftree-partial-pre -fpredictive-commoning -ffunction-sections -fdata-sections -fuse-linker-plugin -pipe -Wno-implicit-function-declaration 


# linker flags used to generate the main object file
LDFLAGS		=  $(CFLAG) -nostdlib  -Wl,--gc-sections -Wl,-O1 -Wl,--as-needed  -Wl,--no-check-sections -u call_user_start -Wl,-static -L$(ESP8266_SDK_ROOT)/esp-open-lwip

# linker script used for the above linkier step
#LD_SCRIPT	= eagle.app.v6.ld
LD_SCRIPT1	= -Trom0.ld
LD_SCRIPT2	= -Trom1.ld

# various paths from the SDK used in this project
SDK_LIBDIR	= sdk/lib
SDK_LDDIR	= ld
SDK_INCDIR	= include include/json

# we create two different files for uploading into the flash
# these are the names and options to generate them
FW_FILE_1_ADDR	= 0x02000
FW_FILE_2_ADDR	= 0x41000

# select which tools to use as compiler, librarian and linker
CC=$(ESP8266_SDK_ROOT)/xtensa-lx106-elf/bin/xtensa-lx106-elf-gcc
AR=$(ESP8266_SDK_ROOT)/xtensa-lx106-elf/bin/xtensa-lx106-elf-ar
LD=$(ESP8266_SDK_ROOT)/xtensa-lx106-elf/bin/xtensa-lx106-elf-gcc



####
#### no user configurable options below here
####
SRC_DIR		:= $(MODULES)
BUILD_DIR	:= $(addprefix $(BUILD_BASE)/,$(MODULES))

SDK_LIBDIR	:= $(addprefix $(ESP8266_SDK_ROOT)/,$(SDK_LIBDIR))
SDK_INCDIR	:= $(addprefix -I$(ESP8266_SDK_ROOT)/,$(SDK_INCDIR))

SRC		:= $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.c))
OBJ		:= $(patsubst %.c,$(BUILD_BASE)/%.o,$(SRC))
LIBS		:= $(addprefix -l,$(LIBS))
APP_AR		:= $(addprefix $(BUILD_BASE)/,$(TARGET)_app.a)
TARGET_OUT	:= $(addprefix $(BUILD_BASE)/,$(TARGET).out)

INCDIR	:= $(addprefix -I,$(SRC_DIR))
EXTRA_INCDIR	:= $(addprefix -I,$(EXTRA_INCDIR))
MODULE_INCDIR	:= $(addsuffix /include,$(INCDIR))

FW_FILE_1	:= $(addprefix $(FW_BASE)/,rom0.bin)
FW_FILE_2	:= $(addprefix $(FW_BASE)/,rom1.bin)
RBOOT_FILE	:= $(addprefix $(FW_BASE)/,rboot.bin)

V ?= $(VERBOSE)
ifeq ("$(V)","1")
Q :=
vecho := @true
else
Q := @
vecho := @echo
endif

vpath %.c $(SRC_DIR)

define compile-objects
$1/%.o: %.c
	if [ -z "$(WPA2_PASSPHRASE)" ] ; then \
		echo "Usage: make WPA2_PASSPHRASE=secret01" ; \
		exit 1 ; \
	fi
	$(vecho) "CC $$<"
	$(Q) $(CC) $(INCDIR) $(MODULE_INCDIR) $(EXTRA_INCDIR) $(SDK_INCDIR) $(CFLAGS) -c $$< -o $$@
endef

.PHONY: all checkdirs clean

#all: checkdirs $(TARGET_OUT) $(FW_FILE_1) $(FW_FILE_2)
all: checkdirs $(FW_FILE_1) $(FW_FILE_2) $(RBOOT_FILE) $(FW_BASE)/sha1sums src/*

$(FW_FILE_1): $(APP_AR)
	$(Q) $(LD) -L$(BUILD_AREA)/esp-open-lwip -L$(SDK_LIBDIR) $(LD_SCRIPT1) $(LDFLAGS) -Wl,--start-group $(LIBS) $(APP_AR) -Wl,--end-group -o $(TARGET_OUT)
	$(ESPTOOL) elf2image --version=2 $(TARGET_OUT) -o $(FW_FILE_1)


$(FW_FILE_2): $(APP_AR)
	$(Q) $(LD) -L$(BUILD_AREA)/esp-open-lwip -L$(SDK_LIBDIR) $(LD_SCRIPT2) $(LDFLAGS) -Wl,--start-group $(LIBS) $(APP_AR) -Wl,--end-group -o $(TARGET_OUT)
	$(ESPTOOL) elf2image --version=2 $(TARGET_OUT) -o $(FW_FILE_2)

$(RBOOT_FILE): rboot.bin
	$(Q) cp rboot.bin $(RBOOT_FILE)


$(FW_BASE)/sha1sums: $(APP_AR) $(FW_FILE_1) $(FW_FILE_2) $(RBOOT_FILE)
	$(Q) sha1sum $(FW_FILE_1) $(FW_FILE_2) $(RBOOT_FILE) > $(FW_BASE)/sha1sums

$(APP_AR): $(OBJ)
	$(vecho) "AR $@"
	$(Q) $(AR) cru $@ $^

checkdirs: $(BUILD_DIR) $(FW_BASE)

$(BUILD_DIR):
	$(Q) mkdir -p $@

$(FW_BASE):
	$(Q) mkdir -p $@

flash: $(FW_BASE)/sha1sums
	$(ESPTOOL) --port $(ESPPORT) --baud $(ESPTOOLBAUD) write_flash $(ESPTOOLOPTS) 0x00000 $(RBOOT_FILE) $(FW_FILE_1_ADDR) $(FW_FILE_1) 0x7b000 esp_data.bin 0x7c000 esp_data.bin

flash1: $(FW_BASE)/sha1sums
	$(ESPTOOL) --port $(ESPPORT) --baud $(ESPTOOLBAUD) write_flash $(ESPTOOLOPTS) 0x00000 $(RBOOT_FILE) $(FW_FILE_2_ADDR) $(FW_FILE_2)

flashboth: $(FW_BASE)/sha1sums
	$(ESPTOOL) --port $(ESPPORT) --baud $(ESPTOOLBAUD) write_flash $(ESPTOOLOPTS) 0x00000 $(RBOOT_FILE) $(FW_FILE_1_ADDR) $(FW_FILE_1) $(FW_FILE_2_ADDR) $(FW_FILE_2)

clean:
	$(Q) rm -rf $(FW_BASE) $(BUILD_BASE)
	$(Q) find . -name "*~" -print0 | xargs -0 rm -rf

$(foreach bdir,$(BUILD_DIR),$(eval $(call compile-objects,$(bdir))))
