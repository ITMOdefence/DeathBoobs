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
          -fno-tree-loop-distribute-patterns

BUILD_DIR := build
LIBRARY := $(BUILD_DIR)/libdeathboobs_storage.a
STORAGE_OBJECT := $(BUILD_DIR)/storage.o
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

check: $(TEST_PROGRAM)
	$(TEST_PROGRAM)

check-host: | $(BUILD_DIR)
	$(CC) -std=c11 -fno-builtin -Wall -Wextra -Werror \
		-Itests/host/include $(CPPFLAGS) \
		src/storage/storage.c tests/host/punos_fakes.c \
		tests/unit/storage/storage_runtime.c -o $(HOST_TEST_PROGRAM)
	$(HOST_TEST_PROGRAM)

$(BUILD_DIR):
	mkdir -p $@

$(STORAGE_OBJECT): src/storage/storage.c include/deathboobs/storage.h \
                   include/deathboobs/format.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TEST_OBJECT): tests/unit/storage/storage_runtime.c \
                include/deathboobs/storage.h include/deathboobs/format.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIBRARY): $(STORAGE_OBJECT)
	rm -f $@
	$(AR) rcs $@ $^

punos-dependencies: $(PUNOS_DIR)/.git
	$(MAKE) -C $(SYSTEM_LIBS)/libfile dependencies
	$(MAKE) -C $(SYSTEM_LIBS)/libfile all

vendor/PUNos/.git:
	$(MAKE) vendor

$(PUNOS_FILE) $(PUNOS_STR) $(PUNOS_MEM) $(PUNOS_CORE) \
$(PUNOS_CRT0) $(PUNOS_CRT) $(PUNOS_SYSCALL): punos-dependencies

$(TEST_PROGRAM): $(TEST_OBJECT) $(LIBRARY) $(PUNOS_FILE) $(PUNOS_STR) \
                 $(PUNOS_MEM) $(PUNOS_CORE) $(PUNOS_CRT0) $(PUNOS_CRT) \
                 $(PUNOS_SYSCALL)
	$(CC) -nostdlib -no-pie -Wl,-e,_start \
		$(PUNOS_CRT0) $(TEST_OBJECT) $(LIBRARY) \
		$(PUNOS_FILE) $(PUNOS_STR) $(PUNOS_MEM) $(PUNOS_CORE) \
		$(PUNOS_CRT) $(PUNOS_SYSCALL) -o $@

clean:
	rm -rf $(BUILD_DIR)
