BUILD = build

all: $(BUILD)
	$(MAKE) -C keygen  BUILD=../$(BUILD)
	$(MAKE) -C server  BUILD=../$(BUILD)
	$(MAKE) -C client  BUILD=../$(BUILD)

server: $(BUILD)
	$(MAKE) -C keygen  BUILD=../$(BUILD)
	$(MAKE) -C server  BUILD=../$(BUILD)

client: $(BUILD)
	$(MAKE) -C keygen  BUILD=../$(BUILD)
	$(MAKE) -C client  BUILD=../$(BUILD)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	$(MAKE) -C keygen  clean BUILD=../$(BUILD)
	$(MAKE) -C server  clean BUILD=../$(BUILD)
	$(MAKE) -C client  clean BUILD=../$(BUILD)
	rm -rf $(BUILD)

.PHONY: all server client clean
