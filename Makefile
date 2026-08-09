BUILD = build

all: $(BUILD)
	$(MAKE) -C protocol/keygen  BUILD=../../$(BUILD)
	$(MAKE) -C server  BUILD=../$(BUILD)
	$(MAKE) -C client  BUILD=../$(BUILD)

server: $(BUILD)
	$(MAKE) -C protocol/keygen  BUILD=../../$(BUILD)
	$(MAKE) -C server  BUILD=../$(BUILD)

client: $(BUILD)
	$(MAKE) -C protocol/keygen  BUILD=../../$(BUILD)
	$(MAKE) -C client  BUILD=../$(BUILD)

$(BUILD):
	mkdir -p $(BUILD)

arm64: $(BUILD)
	$(MAKE) -C protocol/keygen  arm64 BUILD=../../$(BUILD)
	$(MAKE) -C server  arm64 BUILD=../$(BUILD)
	$(MAKE) -C client  arm64 BUILD=../$(BUILD)

VENV = tests/venv

# 1. create the venv if missing; 2. (re)install deps when requirements.txt
#    changes; 3. run the suite with the venv's python
test: $(VENV)/.stamp
	$(VENV)/bin/python tests/run_tests.py

$(VENV)/.stamp: tests/requirements.txt
	python3 -m venv $(VENV)
	$(VENV)/bin/pip install -q -r tests/requirements.txt
	touch $@

clean:
	$(MAKE) -C protocol/keygen  clean BUILD=../../$(BUILD)
	$(MAKE) -C server  clean BUILD=../$(BUILD)
	$(MAKE) -C client  clean BUILD=../$(BUILD)
	rm -rf $(BUILD)

.PHONY: all server client arm64 test clean
