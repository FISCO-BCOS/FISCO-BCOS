# Copyright (c) FISCO-BCOS, Apache-2.0
# Merge the FISCO predeploy overlay onto op-deployer terminal allocs and emit
# the L2 genesis [alloc.N] INI fragment.

# Path to the contracts package (relative to this dir by default).
CONTRACTS ?= ../../bcos-l2-contracts
# op-deployer terminal alloc JSON — the REQUIRED base. Every OP account
# (0x42... proxies, 0xc0d3... implementations, ProxyAdmin ownership,
# prefunded accounts) comes from this file; generating it needs the
# op-deployer binary (see the karst_pin in $(CONTRACTS)/op-fork-pin.toml).
BASE      ?= final-allocs.json
# Path to a clone of the OP fork at the FROZEN Karst pin
# ([karst_pin].op_contracts_commit in $(CONTRACTS)/op-fork-pin.toml). NOT
# needed for `make allocs` — genesis takes OP bytecode from $(BASE). The
# clone/build targets remain for inspecting the pinned sources / cross-
# checking against the deployer output.
OP_FORK   ?= /tmp/op-fork
# chain-config YAML (copy chain-config.template.yaml and edit).
CONFIG    ?= chain-config.yaml
# Output INI fragment + merged alloc JSON (feeds the op-reth oracle genesis).
OUT       ?= allocs.ini
OUT_JSON  ?= allocs.json
PYTHON    ?= python3

.PHONY: all contracts allocs test clean op-fork op-fork-build

all: allocs

# Compile the self-written predeploys (SystemConfig, L2ValidatorSet) — the
# only forge artifacts `make allocs` consumes.
contracts:
	cd $(CONTRACTS) && forge build

# Generate the INI + merged JSON from the self-written forge artifacts, the
# chain config and the op-deployer base allocs.
allocs:
	@if [ ! -f "$(BASE)" ]; then \
		echo "error: base allocs '$(BASE)' not found — point BASE= at the" \
		     "op-deployer terminal alloc JSON (the required genesis base)"; \
		exit 1; \
	fi
	$(PYTHON) build-allocs.py --config $(CONFIG) --contracts $(CONTRACTS) \
	    --base-allocs $(BASE) --out $(OUT) --out-json $(OUT_JSON)

# Clone the OP fork at the frozen Karst pin into $(OP_FORK). No-op if the dir
# already exists — bumping the pin is a manual step (see
# docs/runbook-op-fork-upgrade.md). Note: this reads the [karst_pin] table,
# NOT the top-level legacy compile-check pin CI still consumes.
op-fork:
	@if [ ! -d "$(OP_FORK)" ]; then \
		COMMIT=$$(grep -E '^op_contracts_commit' $(CONTRACTS)/op-fork-pin.toml | awk -F'"' '{print $$2}'); \
		git clone --filter=blob:none https://github.com/ethereum-optimism/optimism $(OP_FORK); \
		git -C $(OP_FORK) checkout $$COMMIT; \
		git -C $(OP_FORK) submodule update --init --recursive --depth=1 \
		    packages/contracts-bedrock/lib; \
	fi

# Compile the pinned OP fork (its own foundry.toml / solc). Not part of the
# allocs pipeline — see the OP_FORK comment above.
op-fork-build: op-fork
	cd $(OP_FORK)/packages/contracts-bedrock && forge build

# Run the pytest suite (synthetic fixtures, no forge/op-deployer run required).
test:
	$(PYTHON) -m pytest test_build_allocs.py -v

clean:
	rm -f $(OUT) $(OUT_JSON)
