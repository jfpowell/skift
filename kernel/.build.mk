CONFIG_KEYBOARD_LAYOUT?=en_us

KERNEL_SOURCES += \
	$(wildcard kernel/*.cpp) \
	$(wildcard kernel/*/*.cpp) \
	$(wildcard kernel/*/*/*.cpp)

KERNEL_ASSEMBLY_SOURCES += \
	$(wildcard kernel/*.s) \
	$(wildcard kernel/*/*.s)

KERNEL_LIBRARIES_SOURCES = \
	$(wildcard libraries/libfile/*.cpp) \
	$(wildcard libraries/libjson/*.cpp) \
	$(wildcard libraries/libsystem/*.cpp) \
	$(wildcard libraries/libsystem/io/*.cpp) \
	$(wildcard libraries/libsystem/compat/ctype.cpp) \
	$(wildcard libraries/libsystem/unicode/*.cpp) \
	$(wildcard libraries/libsystem/process/*.cpp) \
	$(wildcard libraries/libsystem/math/*.cpp) \
	$(wildcard libraries/libsystem/utils/*.cpp) \
	$(wildcard libraries/libsystem/core/*.cpp) \
	$(wildcard libraries/libsystem/thread/*.cpp) \
	$(wildcard libraries/libsystem/system/*.cpp) \
	$(wildcard libraries/libsystem/cxx/new-delete.cpp)

KERNEL_BINARY = $(BOOTROOT)/kernel.bin

KERNEL_OBJECTS = \
	$(patsubst %.cpp, $(BUILD_DIRECTORY)/%.o, $(KERNEL_SOURCES)) \
	$(patsubst %.s, $(BUILD_DIRECTORY)/%.s.o, $(KERNEL_ASSEMBLY_SOURCES)) \
	$(patsubst libraries/%.cpp, $(BUILD_DIRECTORY)/kernel/%.o, $(KERNEL_LIBRARIES_SOURCES))

OBJECTS += $(KERNEL_OBJECTS)

$(BUILD_DIRECTORY)/kernel/%.o: libraries/%.cpp
	$(DIRECTORY_GUARD)
	@echo [KERNEL] [CXX] $<
	@$(CXX) $(CXXFLAGS) -ffreestanding -nostdlib -c -o $@ $<

$(BUILD_DIRECTORY)/kernel/%.o: kernel/%.cpp
	$(DIRECTORY_GUARD)
	@echo [KERNEL] [CXX] $<
	@$(CXX) -DCONFIG_KEYBOARD_LAYOUT=\""${CONFIG_KEYBOARD_LAYOUT}"\" $(CXXFLAGS) -ffreestanding -nostdlib -c -o $@ $<

$(BUILD_DIRECTORY)/arch/%.o: arch/%.cpp
	$(DIRECTORY_GUARD)
	@echo [KERNEL] [CXX] $<
	@$(CXX) $(CXXFLAGS) -ffreestanding -nostdlib -c -o $@ $<

$(BUILD_DIRECTORY)/arch/%.s.o: arch/%.s
	$(DIRECTORY_GUARD)
	@echo [KERNEL] [AS] $<
	@$(AS) $(ASFLAGS) $^ -o $@

$(KERNEL_BINARY): $(KERNEL_OBJECTS)
	$(DIRECTORY_GUARD)
	@echo [KERNEL] [LD] $(KERNEL_BINARY)
	@$(CXX) $(LDFLAGS) -T arch/x86_32/link.ld -o $@ -ffreestanding $^ -nostdlib -lgcc
