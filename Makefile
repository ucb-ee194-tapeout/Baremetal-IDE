# To generate the header: PMU_MANIFEST=/path/tile.pmu.json PMU_PLATFORM=/path/config.json
# Poll by name: PMU_POLL_COUNTER_ID=TILE_PMU_EVENT_<EVENT_NAME>
# PMU off by default. Enable collection with PMU_ENABLE=1; add
# PMU_POLL_ENABLE=1 for execution-time snapshots. PMU_POLL_COUNTER_ID accepts
# an integer or an event macro from ATLAS_PMU_HEADER=/absolute/path/to/header.h.
PMU_MANIFEST ?=
PMU_PLATFORM ?=
PMU_SOFTWARE_DIR ?= $(abspath ../pmu/software)
ATLAS_PMU_HEADER ?=
PMU_ENABLE ?= 0
PMU_POLL_ENABLE ?= 0
PMU_POLL_COUNTER_ID ?= 0
PMU_POLL_INTERVAL ?= 1000
PMU_POLL_PRINT ?= 0
PMU_CMAKE_ARGS = -DPMU_ENABLE=$(PMU_ENABLE) -DPMU_POLL_ENABLE=$(PMU_POLL_ENABLE) \
 -DPMU_POLL_COUNTER_ID=$(PMU_POLL_COUNTER_ID) -DPMU_POLL_INTERVAL=$(PMU_POLL_INTERVAL) \
 -DPMU_POLL_PRINT=$(PMU_POLL_PRINT) -DATLAS_PMU_HEADER="$(ATLAS_PMU_HEADER)" \
 -DPMU_MANIFEST="$(PMU_MANIFEST)" -DPMU_PLATFORM="$(PMU_PLATFORM)" \
 -DPMU_SOFTWARE_DIR="$(PMU_SOFTWARE_DIR)"

UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  PREFIX = riscv64-elf-
else
  PREFIX = riscv64-unknown-elf-
endif

CC = $(PREFIX)gcc
CXX = $(PREFIX)g++
CP = $(PREFIX)objcopy
OD = $(PREFIX)objdump
DG = $(PREFIX)gdb
SIZE = $(PREFIX)size

PORT = 3333
TYPE = Release

.PHONY: build
build:
	cmake -S ./ -B ./build/ -D CMAKE_BUILD_TYPE=$(TYPE) -D CMAKE_TOOLCHAIN_FILE=./riscv-gcc.cmake -DCHIP=$(CHIP) $(if $(PLATFORM), -D PLATFORM=$(PLATFORM),) $(if $(VECNN), -D BUILD_VECNN=$(VECNN),) $(if $(RVV), -D ENABLE_RVV=$(RVV),) $(if $(RVV_TYPE), -D RVV_TYPE=$(RVV_TYPE),) $(if $(THREAD_LIB), -D THREAD_LIB=$(THREAD_LIB),) $(if $(BMARK_LIB), -D BMARK_LIB=$(BMARK_LIB),) $(EXTRA_CMAKE_ARGS) $(PMU_CMAKE_ARGS)
	cmake --build ./build/ --target $(TARGET)

.PHONY: ocd
ocd:
	openocd -f ./platform/$(CHIP)/$(CHIP).cfg

.PHONY: ocd-run
ocd-run:
	openocd -f ./platform/$(CHIP)/$(CHIP).cfg -c "reset run" -c "halt" -c "load_image $(BINARY)" -c "resume 0x80000000"

.PHONY: gdb
gdb:
	$(DG) $(BINARY) --eval-command="target extended-remote localhost:$(PORT)"
	# --eval-command="monitor reset"

.PHONY: clean
clean:
	rm -rf build

.PHONY: dump
dump:
	$(OD) -D  $(BINARY) > $(BINARY).dump

.PHONY: checktsi
checktsi:
	uart_tsi +tty=$(TTY) +baudrate=921600 +no_hart0_msip +init_write=0x80001000:0xb0bacafe +init_read=0x80001000 none

.PHONY: tsi-run
tsi-run:
	uart_tsi +tty=$(TTY) +baudrate=921600 $(BINARY)


