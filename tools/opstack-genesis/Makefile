# Copyright (c) FISCO-BCOS, Apache-2.0
# Build L2 genesis [alloc.N] INI fragments from the bcos-l2-contracts forge build.

# Path to the contracts package (relative to this dir by default). The package
# itself ships in a sibling PR (feat/l2-a6-pr1-contracts → bcos-l2-contracts/);
# this Makefile is git-tracked here but only `make` works once that PR lands.
CONTRACTS ?= ../../bcos-l2-contracts
# Path to a clone of the pinned OP fork. Read from
# $(CONTRACTS)/op-fork-pin.toml; CI clones into /tmp/op-fork, local users
# override via OP_FORK=...
OP_FORK   ?= /tmp/op-fork
# chain-config YAML (copy chain-config.template.yaml and edit).
CONFIG    ?= chain-config.yaml
# Output INI fragment.
OUT       ?= allocs.ini
PYTHON    ?= python3

.PHONY: all contracts allocs test clean op-fork

all: allocs

# Clone the pinned OP fork into $(OP_FORK). No-op if the dir already exists —
# bumping the pin is a manual step (see docs/runbook-op-fork-upgrade.md).
op-fork:
	@if [ ! -d "$(OP_FORK)" ]; then \
		COMMIT=$$(grep -E '^commit' $(CONTRACTS)/op-fork-pin.toml | awk -F'"' '{print $$2}'); \
		git clone --filter=blob:none https://github.com/ethereum-optimism/optimism $(OP_FORK); \
		git -C $(OP_FORK) checkout $$COMMIT; \
		git -C $(OP_FORK) submodule update --init --recursive --depth=1 \
		    packages/contracts-bedrock/lib; \
	fi

# Compile the self-written predeploys, then the pinned OP fork. The OP fork is
# a separate compile unit (its own foundry.toml + solc 0.8.15) so we cd into
# its contracts-bedrock root for that build.
contracts: op-fork
	cd $(CONTRACTS) && forge build
	cd $(OP_FORK)/packages/contracts-bedrock && forge build

# Generate the INI from the (already built) forge artifacts + chain config.
allocs:
	$(PYTHON) build-allocs.py --config $(CONFIG) --contracts $(CONTRACTS) --out $(OUT)

# Run the pytest suite (synthetic fixtures, no forge run required).
test:
	$(PYTHON) -m pytest test_build_allocs.py -v

clean:
	rm -f $(OUT)
