CC ?= cc
AR ?= ar
PUNOS_DIR ?= vendor/PUNos

SYSTEM_LIBS := $(PUNOS_DIR)/System/SystemLibs
NATIVE_DIR := $(PUNOS_DIR)/System/NativeRuntime
SYSCALL_DIR := $(NATIVE_DIR)/SyscallWrappers
CRT_DIR := $(NATIVE_DIR)/CRT

CPPFLAGS := -Iinclude \
             -I$(SYSTEM_LIBS) \
             -I$(SYSTEM_LIBS)/libcore/include \
             -I$(SYSTEM_LIBS)/libmem/include \
             -I$(SYSTEM_LIBS)/libstr/include \
             -I$(SYSTEM_LIBS)/libfile/include \
             -I$(SYSCALL_DIR)/include \
             -I$(NATIVE_DIR)/include
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin \
          -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror \
          -fno-tree-loop-distribute-patterns -minline-all-stringops

BUILD_DIR := build
LIBRARY := $(BUILD_DIR)/libdeathboobs_storage.a
STORAGE_OBJECT := $(BUILD_DIR)/storage.o
TRANSACTION_SOURCES := src/transaction/transaction.c src/transaction/journal.c
TRANSACTION_OBJECTS := $(BUILD_DIR)/transaction.o $(BUILD_DIR)/journal.o
INTEGRATION_OBJECT := $(BUILD_DIR)/transaction_runtime.o
INTEGRATION_PROGRAM := $(BUILD_DIR)/transaction_runtime
HOST_INTEGRATION := $(BUILD_DIR)/transaction_host_test
HOST_FAULTS := $(BUILD_DIR)/transaction_fault_test
HOST_CPPFLAGS := -Itests/host/include $(CPPFLAGS)
HOST_CFLAGS := -std=c11 -fno-builtin -Wall -Wextra -Werror
HOST_SOURCES := src/storage/storage.c tests/host/punos_fakes.c
TEST_OBJECT := $(BUILD_DIR)/storage_runtime.o
TEST_PROGRAM := $(BUILD_DIR)/storage_runtime
HOST_TEST_PROGRAM := $(BUILD_DIR)/storage_host_test

PUNOS_FILE := $(SYSTEM_LIBS)/libfile/build/libpunos_file.a
PUNOS_STR := $(SYSTEM_LIBS)/libstr/build/libpunos_str.a
PUNOS_MEM := $(SYSTEM_LIBS)/libmem/build/libpunos_mem.a
PUNOS_CORE := $(SYSTEM_LIBS)/libcore/build/libpunos_core.a
PUNOS_CRT0 := $(CRT_DIR)/build/crt0.o
PUNOS_CRT := $(CRT_DIR)/build/libpunos_crt.a
PUNOS_SYSCALL := $(SYSCALL_DIR)/build/libpunos_syscall.a

.PHONY: all check check-host clean vendor punos-dependencies

all: $(LIBRARY)

vendor:
	@test -d vendor/PUNos/.git || git clone https://github.com/ITMOdefence/PUNos.git vendor/PUNos
	git -C vendor/PUNos fetch --depth 1 origin $$(cat punos.ref)
	git -C vendor/PUNos checkout --detach $$(cat punos.ref)

check: $(TEST_PROGRAM) $(INTEGRATION_PROGRAM)
	$(TEST_PROGRAM)
	$(INTEGRATION_PROGRAM)

check-host: | $(BUILD_DIR)
	$(CC) $(HOST_CFLAGS) $(HOST_CPPFLAGS) $(HOST_SOURCES) \
		tests/unit/storage/storage_runtime.c -o $(HOST_TEST_PROGRAM)
	$(HOST_TEST_PROGRAM)
	$(CC) $(HOST_CFLAGS) $(HOST_CPPFLAGS) $(HOST_SOURCES) $(TRANSACTION_SOURCES) \
		tests/integration/transaction_runtime.c -o $(HOST_INTEGRATION)
	$(HOST_INTEGRATION)
	$(CC) $(HOST_CFLAGS) $(HOST_CPPFLAGS) $(HOST_SOURCES) $(TRANSACTION_SOURCES) \
		tests/recovery/transaction_faults.c -o $(HOST_FAULTS)
	$(HOST_FAULTS)

$(BUILD_DIR):
	mkdir -p $@

$(STORAGE_OBJECT): src/storage/storage.c include/deathboobs/storage.h \
                   include/deathboobs/format.h src/internal/page_codec.h \
                   src/internal/writer_lock.h Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TEST_OBJECT): tests/unit/storage/storage_runtime.c Makefile \
                include/deathboobs/storage.h include/deathboobs/format.h Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/writer_lock.o: src/storage/writer_lock.c src/internal/writer_lock.h Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/transaction/%.c src/transaction/journal.h src/internal/page_codec.h \
                  include/deathboobs/transaction.h include/deathboobs/storage.h \
                  include/deathboobs/format.h Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(INTEGRATION_OBJECT): tests/integration/transaction_runtime.c \
                       include/deathboobs/transaction.h Makefile | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DDEATHBOOBS_NATIVE_TEST -c $< -o $@

$(LIBRARY): $(STORAGE_OBJECT) $(BUILD_DIR)/writer_lock.o $(TRANSACTION_OBJECTS)
	rm -f $@
	$(AR) rcs $@ $^

punos-dependencies: $(PUNOS_DIR)/.git
	$(MAKE) -C $(SYSTEM_LIBS)/libfile BUILD_DIR=build dependencies
	$(MAKE) -C $(SYSTEM_LIBS)/libfile BUILD_DIR=build all

vendor/PUNos/.git:
	$(MAKE) vendor

$(PUNOS_FILE) $(PUNOS_STR) $(PUNOS_MEM) $(PUNOS_CORE) \
$(PUNOS_CRT0) $(PUNOS_CRT) $(PUNOS_SYSCALL): punos-dependencies

$(INTEGRATION_PROGRAM): TEST_LDFLAGS := -Wl,--wrap=file_remove \
    -Wl,--wrap=deathboobs_storage_page_write

$(TEST_PROGRAM) $(INTEGRATION_PROGRAM): %: %.o $(LIBRARY) $(PUNOS_FILE) $(PUNOS_STR) \
    $(PUNOS_MEM) $(PUNOS_CORE) $(PUNOS_CRT0) $(PUNOS_CRT) $(PUNOS_SYSCALL)
	$(CC) -nostdlib -no-pie -Wl,-e,_start $(TEST_LDFLAGS) \
		$(PUNOS_CRT0) $< $(LIBRARY) \
		$(PUNOS_FILE) $(PUNOS_STR) $(PUNOS_MEM) $(PUNOS_CORE) \
		$(PUNOS_CRT) $(PUNOS_SYSCALL) -o $@

clean:
	rm -rf $(BUILD_DIR)
