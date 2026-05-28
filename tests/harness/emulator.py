"""MiniOSEmulator — Unicorn Engine wrapper for testing 16-bit real-mode routines.

Provides a high-level API for loading assembled stubs, setting up memory,
executing code, and inspecting results.
"""

from pathlib import Path
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_16
from unicorn.x86_const import (
    UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
    UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_SP, UC_X86_REG_BP,
    UC_X86_REG_IP, UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES,
    UC_X86_REG_SS, UC_X86_REG_FLAGS,
    UC_X86_REG_AH, UC_X86_REG_AL,
    UC_X86_REG_CH, UC_X86_REG_CL,
    UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_EDI, UC_X86_REG_ESI,
)
from unicorn import UC_HOOK_CODE

from .constants import CODE_BASE, STACK_TOP


# x86 FLAGS register bits
FLAG_ZF = 0x0040   # Zero Flag
FLAG_CF = 0x0001   # Carry Flag


class EmulatorTimeout(Exception):
    """Raised when emulation exceeds the instruction limit."""
    pass


class MiniOSEmulator:
    """Unicorn-based emulator for testing 16-bit real-mode routines."""

    def __init__(self, code_base: int = CODE_BASE):
        self.code_base = code_base
        self.code_size = 0
        self._executed_addrs: set[int] = set()
        self._executed_edges: set[tuple[int, int]] = set()
        self._prev_addr: int | None = None
        self._instruction_count = 0

        # Create 16-bit real-mode emulator
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)

        # Map 1 MB of memory (full real-mode address space)
        self.uc.mem_map(0, 0x100000)

        # Set up stack
        self.uc.reg_write(UC_X86_REG_SP, STACK_TOP)
        self.uc.reg_write(UC_X86_REG_SS, 0)
        self.uc.reg_write(UC_X86_REG_DS, 0)
        self.uc.reg_write(UC_X86_REG_ES, 0)
        self.uc.reg_write(UC_X86_REG_CS, 0)

        # Install coverage hook
        self.uc.hook_add(UC_HOOK_CODE, self._hook_code)

    def _hook_code(self, uc, address, size, user_data):
        """Instruction-level hook for coverage, edge tracking, and timeout."""
        self._executed_addrs.add(address)
        if self._prev_addr is not None:
            self._executed_edges.add((self._prev_addr, address))
        self._prev_addr = address
        self._instruction_count += 1
        if self._instruction_count > 100_000:
            uc.emu_stop()
            raise EmulatorTimeout(
                f"Exceeded 100,000 instructions (possible infinite loop) "
                f"at address 0x{address:04X}"
            )

    def load(self, binary_path: str | Path):
        """Load a flat binary into emulated memory at code_base."""
        data = Path(binary_path).read_bytes()
        self.code_size = len(data)
        self.uc.mem_write(self.code_base, data)

    def run(self, entry_offset: int = 0, timeout_us: int = 5_000_000):
        """Execute code from code_base + entry_offset until HLT or timeout.

        Args:
            entry_offset: Offset from code_base to start execution.
            timeout_us: Timeout in microseconds (default 5 seconds).
        """
        entry = self.code_base + entry_offset
        # End address: we rely on HLT to stop, set end past the binary
        end = self.code_base + self.code_size + 0x1000
        self._instruction_count = 0
        try:
            self.uc.emu_start(entry, end, timeout=timeout_us)
        except UcError as e:
            # HLT instruction causes UC_ERR_INSN_INVALID on some Unicorn
            # versions — that's expected and means the routine completed.
            if "HLT" in str(e).upper() or "INVALID" in str(e).upper():
                pass
            else:
                raise

    # --- Memory access --------------------------------------------------------

    def write_byte(self, addr: int, val: int):
        self.uc.mem_write(addr, bytes([val & 0xFF]))

    def read_byte(self, addr: int) -> int:
        return self.uc.mem_read(addr, 1)[0]

    def write_word(self, addr: int, val: int):
        self.uc.mem_write(addr, (val & 0xFFFF).to_bytes(2, "little"))

    def read_word(self, addr: int) -> int:
        data = bytes(self.uc.mem_read(addr, 2))
        return int.from_bytes(data, "little")

    def write_string(self, addr: int, s: str):
        """Write a NUL-terminated ASCII string to memory."""
        self.uc.mem_write(addr, s.encode("ascii") + b"\x00")

    def read_string(self, addr: int, max_len: int = 256) -> str:
        """Read a NUL-terminated string from memory."""
        data = bytes(self.uc.mem_read(addr, max_len))
        nul = data.find(0)
        if nul >= 0:
            data = data[:nul]
        return data.decode("ascii", errors="replace")

    def write_bytes(self, addr: int, data: bytes):
        self.uc.mem_write(addr, data)

    def read_bytes(self, addr: int, size: int) -> bytes:
        return bytes(self.uc.mem_read(addr, size))

    # --- Register access ------------------------------------------------------

    def reg(self, name: str) -> int:
        """Read a register by name (e.g., 'ax', 'si', 'flags', 'eax')."""
        reg_map = {
            "ax": UC_X86_REG_AX, "bx": UC_X86_REG_BX,
            "cx": UC_X86_REG_CX, "dx": UC_X86_REG_DX,
            "si": UC_X86_REG_SI, "di": UC_X86_REG_DI,
            "sp": UC_X86_REG_SP, "bp": UC_X86_REG_BP,
            "ip": UC_X86_REG_IP, "flags": UC_X86_REG_FLAGS,
            "cs": UC_X86_REG_CS, "ds": UC_X86_REG_DS,
            "es": UC_X86_REG_ES, "ss": UC_X86_REG_SS,
            "ah": UC_X86_REG_AH, "al": UC_X86_REG_AL,
            "ch": UC_X86_REG_CH, "cl": UC_X86_REG_CL,
            "eax": UC_X86_REG_EAX, "ebx": UC_X86_REG_EBX,
            "ecx": UC_X86_REG_ECX, "edx": UC_X86_REG_EDX,
            "edi": UC_X86_REG_EDI, "esi": UC_X86_REG_ESI,
        }
        key = name.lower()
        if key not in reg_map:
            raise ValueError(f"Unknown register: {name}")
        return self.uc.reg_read(reg_map[key])

    def set_reg(self, name: str, val: int):
        """Write a register by name."""
        reg_map = {
            "ax": UC_X86_REG_AX, "bx": UC_X86_REG_BX,
            "cx": UC_X86_REG_CX, "dx": UC_X86_REG_DX,
            "si": UC_X86_REG_SI, "di": UC_X86_REG_DI,
            "sp": UC_X86_REG_SP, "bp": UC_X86_REG_BP,
            "cs": UC_X86_REG_CS, "ds": UC_X86_REG_DS,
            "es": UC_X86_REG_ES, "ss": UC_X86_REG_SS,
            "eax": UC_X86_REG_EAX, "ebx": UC_X86_REG_EBX,
            "ecx": UC_X86_REG_ECX, "edx": UC_X86_REG_EDX,
            "edi": UC_X86_REG_EDI, "esi": UC_X86_REG_ESI,
        }
        key = name.lower()
        if key not in reg_map:
            raise ValueError(f"Unknown register: {name}")
        # 32-bit regs get full value; 16-bit get masked
        if key.startswith('e'):
            self.uc.reg_write(reg_map[key], val & 0xFFFFFFFF)
        else:
            self.uc.reg_write(reg_map[key], val & 0xFFFF)

    # --- Flags helpers --------------------------------------------------------

    @property
    def zf(self) -> bool:
        """Zero Flag state."""
        return bool(self.reg("flags") & FLAG_ZF)

    @property
    def cf(self) -> bool:
        """Carry Flag state."""
        return bool(self.reg("flags") & FLAG_CF)

    # --- Coverage -------------------------------------------------------------

    @property
    def executed_addresses(self) -> set[int]:
        """Set of all instruction addresses executed."""
        return self._executed_addrs.copy()

    @property
    def coverage_in_binary(self) -> set[int]:
        """Executed addresses that fall within the loaded binary."""
        end = self.code_base + self.code_size
        return {a for a in self._executed_addrs if self.code_base <= a < end}

    @property
    def edges_in_binary(self) -> set[tuple[int, int]]:
        """Executed edges (from→to) where both endpoints are within the binary."""
        end = self.code_base + self.code_size
        return {
            (f, t) for f, t in self._executed_edges
            if self.code_base <= f < end and self.code_base <= t < end
        }
