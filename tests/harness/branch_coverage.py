"""Branch coverage analysis for 16-bit x86 binaries.

Uses Capstone to disassemble binaries and identify conditional branches,
then checks edge coverage data to determine if both outcomes (taken and
fall-through) were exercised.
"""

from pathlib import Path
from dataclasses import dataclass

from capstone import Cs, CS_ARCH_X86, CS_MODE_16, CS_GRP_JUMP


# Conditional jump mnemonics in x86-16 (excludes unconditional 'jmp')
_CONDITIONAL_JUMPS = frozenset([
    "jo", "jno", "jb", "jnb", "jz", "jnz", "jbe", "jnbe",
    "js", "jns", "jp", "jnp", "jl", "jnl", "jle", "jnle",
    "je", "jne", "ja", "jna", "jae", "jnae", "jc", "jnc",
    "jg", "jge",
    "jcxz", "jecxz",
    "loop", "loope", "loopne", "loopz", "loopnz",
])


@dataclass
class BranchInfo:
    """Information about a single conditional branch."""
    address: int          # Address of the branch instruction
    size: int             # Instruction size in bytes
    mnemonic: str         # e.g., "jz", "loop"
    target: int           # Branch target address
    fallthrough: int      # Fall-through address (address + size)
    taken_covered: bool = False
    fallthrough_covered: bool = False

    @property
    def fully_covered(self) -> bool:
        return self.taken_covered and self.fallthrough_covered

    @property
    def partially_covered(self) -> bool:
        return (self.taken_covered or self.fallthrough_covered) and not self.fully_covered

    @property
    def not_executed(self) -> bool:
        return not self.taken_covered and not self.fallthrough_covered


def find_branches(binary_path: Path | str, load_base: int = 0x1000) -> list[BranchInfo]:
    """Disassemble a binary and find all conditional branch instructions.

    Args:
        binary_path: Path to the flat binary file.
        load_base: Address where the binary is loaded in memory.

    Returns:
        List of BranchInfo for each conditional branch found.
    """
    data = Path(binary_path).read_bytes()
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True

    branches = []
    for insn in md.disasm(data, load_base):
        if insn.mnemonic in _CONDITIONAL_JUMPS:
            # For conditional jumps, the operand is the target address
            # Capstone provides it in insn.operands[0].imm for near jumps
            if insn.operands and insn.operands[0].type == 2:  # CS_OP_IMM
                target = insn.operands[0].imm & 0xFFFF  # 16-bit wrap
            else:
                # Fallback: try parsing from instruction string
                continue

            branches.append(BranchInfo(
                address=insn.address,
                size=insn.size,
                mnemonic=insn.mnemonic,
                target=target,
                fallthrough=insn.address + insn.size,
            ))

    return branches


def analyze_branch_coverage(
    branches: list[BranchInfo],
    edges: set[tuple[int, int]],
    executed_addrs: set[int] | None = None,
) -> dict:
    """Analyze branch coverage using edge data.

    Args:
        branches: List of branches from find_branches().
        edges: Set of (from_addr, to_addr) edges observed during execution.
        executed_addrs: Optional set of executed addresses (for "not executed" detection).

    Returns:
        Summary dict with counts and per-branch details.
    """
    for branch in branches:
        # Check if the taken edge (branch_addr → target) was observed
        branch.taken_covered = (branch.address, branch.target) in edges
        # Check if the fall-through edge (branch_addr → next_instr) was observed
        branch.fallthrough_covered = (branch.address, branch.fallthrough) in edges

    total = len(branches)
    full = sum(1 for b in branches if b.fully_covered)
    partial = sum(1 for b in branches if b.partially_covered)
    not_exec = sum(1 for b in branches if b.not_executed)

    # Branch coverage percentage: each branch has 2 outcomes
    total_outcomes = total * 2
    covered_outcomes = sum(
        (1 if b.taken_covered else 0) + (1 if b.fallthrough_covered else 0)
        for b in branches
    )
    pct = (covered_outcomes / total_outcomes * 100) if total_outcomes > 0 else 100.0

    return {
        "branches_total": total,
        "branches_full": full,
        "branches_partial": partial,
        "branches_not_executed": not_exec,
        "outcomes_total": total_outcomes,
        "outcomes_covered": covered_outcomes,
        "branch_coverage_pct": round(pct, 1),
        "details": [
            {
                "address": f"0x{b.address:04X}",
                "mnemonic": b.mnemonic,
                "target": f"0x{b.target:04X}",
                "fallthrough": f"0x{b.fallthrough:04X}",
                "taken_covered": b.taken_covered,
                "fallthrough_covered": b.fallthrough_covered,
            }
            for b in branches
        ],
    }
