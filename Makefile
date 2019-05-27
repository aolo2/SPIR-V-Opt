APP_NAME = thesis

RELEASE_BUILD_PATH = build/release
DEBUG_BUILD_PATH = build/debug

CC = gcc

DEBUG_CFLAGS = -g -Wall -Wextra -pedantic
RELEASE_CFLAGS = -O2

CFLAGS = $(DEBUG_CFLAGS) -DVK_USE_PLATFORM_XCB_KHR
BUILD_PATH = $(DEBUG_BUILD_PATH)

LDFLAGS = -L./1.1.85.0/lib `pkg-config --static --libs glfw3` `pkg-config --cflags --libs xcb` -lvulkan
INCLUDE = -I./1.1.85.0/x86_64/include

all:
	@mkdir -p $(BUILD_PATH)
	@/usr/bin/time -f"[TIME] %E" $(CC) $(CFLAGS) main.c -o $(BUILD_PATH)/$(APP_NAME).new $(INCLUDE) $(LDFLAGS)
	@rm -f $(BUILD_PATH)/$(APP_NAME)
	@mv $(BUILD_PATH)/$(APP_NAME).new $(BUILD_PATH)/$(APP_NAME)

run:
	@/usr/bin/time -f"[TIME] %E" ./$(BUILD_PATH)/$(APP_NAME)