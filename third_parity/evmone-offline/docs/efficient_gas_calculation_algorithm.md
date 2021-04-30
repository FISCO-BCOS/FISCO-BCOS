# Efficient gas calculation algorithm for EVM

This article describes how to efficiently calculate gas and check stack requirements
for Ethereum Virtual Machine (EVM) instructions.


## Instructions metadata

Let's start by defining some basic and universal instructions' parameters.

1. Base gas cost.
   
   This is the static gas cost of instructions. Some instructions may have 
   _additional_ cost depending on their operand values and/or the environment
   — these have to be handled individually during the instruction execution.
   The _final_ cost is never less than the _base_ cost.
   
2. Stack height requirement.

   This is the minimum stack height (number of items on the stack) 
   required prior to the instruction execution.
   
3. Stack height change.

   This is difference of the stack height before and after the instruction 
   execution. Can be negative if the instruction pops more items than pushes.
   
Examples:

| opcode  | base gas cost | stack height requirement | stack height change |
| ------- | ------------- | ------------------------ | ------------------- |
| ADD     | 3             | 2                        | -1                  |
| EXP     | 50            | 2                        | -1                  |
| DUP4    | 3             | 4                        | 1                   |
| SWAP1   | 3             | 2                        | 0                   |
| ADDRESS | 2             | 0                        | 1                   |
| CALL    | 700           | 7                        | -6                  |


## Basic instruction blocks

A _basic instruction block_ is a sequence of "straight-line" instructions 
without jumps and jumpdests in the middle.
Jumpdests are only allowed at the entry, jumps at the exit.
Basic blocks are nodes in the _control flow graph_.
See [Basic Block] in Wikipedia.

In EVM there are simple rules to identify basic instruction block boundaries:

1. A basic instruction block _starts_ right before:
   - the first instruction in the code,
   - `JUMPDEST` instruction.

2. A basic instruction block _ends_ after the "terminator" instructions:
   - `JUMP`,
   - `JUMPI`,
   - `STOP`,
   - `RETURN`,
   - `REVERT`,
   - `SELFDESTRUCT`.

A basic instruction block is a shortest sequence of instructions such that 
a basic block starts before the first instruction and ends after the last.

In some cases multiple of the above rules can apply to single basic instruction 
block boundary.

## Algorithm

The algorithm for calculating gas and checking stack requirements pre-computes
the values for basic instruction blocks and during execution the checks 
are done only once per instruction block.

### Collecting requirements for basic blocks

For a basic block we need to collect the following information:

- total **base gas cost** of all instructions,
- the **stack height required** (the minimum stack height needed to execute all 
  instructions in the block),
- the **maximum stack height growth** relative to the stack height at block 
  start.

This is done as follows:

1. Split code into basic blocks.
2. For each basic block:

```python
class Instruction:
    base_gas_cost = 0
    stack_required = 0
    stack_change = 0

class BasicBlock:
    base_gas_cost = 0
    stack_required = 0
    stack_max_growth = 0

def collect_basic_block_requirements(basic_block):
    stack_change = 0
    for instruction in basic_block:
        basic_block.base_gas_cost += instruction.base_gas_cost
        
        current_stack_required = instruction.stack_required - stack_change
        basic_block.stack_required = max(basic_block.stack_required, current_stack_required)
        
        stack_change += instruction.stack_change
        
        basic_block.stack_max_growth = max(basic_block.stack_max_growth, stack_change)
```

### Checking basic block requirements

During execution, before executing an instruction that starts a basic block,
the basic block requirements must be checked.

```python
class ExecutionState:
    gas_left = 0
    stack = []

def check_basic_block_requirements(state, basic_block):
    state.gas_left -= basic_block.base_gas_cost
    if state.gas_left < 0:
        raise OutOfGas()
    
    if len(state.stack) < basic_block.stack_required:
        raise StackUnderflow()
    
    if len(state.stack) + basic_block.stack_max_growth > 1024:
        raise StackOverflow()
```

## Misc

### EVM may terminate earlier

Because requirements for a whole basic block are checked up front, the instructions
that have observable external effects might not be executed although they would be
executed if the gas counting would have been done per instruction.
This is not a consensus issue because the execution terminates with a "hard" exception
anyway (and all effects are reverted) but might produce unexpected traces 
or terminate with a different exception type.

### Current "gas left" value

In EVMJIT additional instructions that begin a basic block are `GAS` and any of the _call_ instructions. This is because
these instructions need to know the precise _gas left_ counter value. 
However, in evmone this problem has been solved without additional blocks splitting 
by attaching the correction value to the mentioned instructions.

### Undefined instructions

Undefined instructions have base gas cost 0 and not stack requirements.




[Basic Block]: https://en.wikipedia.org/wiki/Basic_block


   
