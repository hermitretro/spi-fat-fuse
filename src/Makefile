### Project name (also used for output file name)
PROJECT	= avrfp

### Source files and search directory
CSRC    = spi_fat_fuse.c ff.c sdmm.c bcm2835.c
ASRC    =
VPATH   =

### Target device
DEVICE  = 

### Optimization level (0, 1, 2, 3, 4 or s)
OPTIMIZE = s

### C Standard level (c89, gnu89, c99 or gnu99)
CSTD = gnu89

### Include dirs, library dirs and definitions
LIBS	= -lwiringPi -lfuse3
LIBDIRS	= -L/usr/local/lib
INCDIRS	= -I/usr/local/include
DEFS	= _FILE_OFFSET_BITS=64 
ADEFS	=

### Warning contorls
WARNINGS = all extra error

### Output directory
OBJDIR = obj

### Programs to build porject
CC      = gcc
OBJCOPY = objcopy
OBJDUMP = objdump
SIZE    = size
NM      = nm


# Define all object files
COBJ      = $(CSRC:.c=.o) 
AOBJ      = $(ASRC:.S=.o)
COBJ      := $(addprefix $(OBJDIR)/,$(COBJ))
AOBJ      := $(addprefix $(OBJDIR)/,$(AOBJ))
PROJECT   := $(OBJDIR)/$(PROJECT)


# Flags for C files
CFLAGS += -std=$(CSTD)
CFLAGS += -g$(DEBUG)
CFLAGS += $(addprefix -W,$(WARNINGS))
CFLAGS += $(addprefix -I,$(INCDIRS))
CFLAGS += $(addprefix -D,$(DEFS))
CFLAGS += -Wp,-MM,-MP,-MT,$(OBJDIR)/$(*F).o,-MF,$(OBJDIR)/$(*F).d


# Assembler flags
ASFLAGS += $(addprefix -D,$(ADEFS)) -Wa,-gstabs,-g$(DEBUG)
ALL_ASFLAGS = -mmcu=$(DEVICE) -I. -x assembler-with-cpp $(ASFLAGS)


# Linker flags
LDFLAGS += -Wl,-Map,$(PROJECT).map


# Default target.
all: version build

build: elf
elf: $(PROJECT).elf

# Display compiler version information.
version :
	@$(CC) --version

# Link: create ELF output file from object files.
%.elf:  $(AOBJ) $(COBJ)
	@echo
	@echo Linking...
	$(CC) $(CFLAGS) $(AOBJ) $(COBJ) $(LIBS) -o $@

# Compile: create object files from C source files. ARM or Thumb(-2)
$(COBJ) : $(OBJDIR)/%.o : %.c
	@echo
	@echo $< :
	$(CC) -c $(CFLAGS) $< -o $@

# Assemble: create object files from assembler source files. ARM or Thumb(-2)
$(AOBJ) : $(OBJDIR)/%.o : %.S
	@echo
	@echo $< :
	$(CC) -c $(ALL_ASFLAGS) $< -o $@


# Target: clean project.
clean:
	@echo
	rm -f -r $(OBJDIR) | exit 0


# Include the dependency files.
-include $(shell mkdir $(OBJDIR) 2>/dev/null) $(wildcard $(OBJDIR)/*.d)

