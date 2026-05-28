"""Unit tests for the MM heap allocator (mm_alloc, mm_free, mm_avail, mm_info).

Tests the first-fit allocator, block splitting, forward coalescing on free,
boundary conditions, and error handling — all via Unicorn Engine emulation.
"""

import pytest
from pathlib import Path
from tests.harness.emulator import MiniOSEmulator
from tests.harness.assembler import assemble_stub
from tests.conftest import register_coverage
from tests.harness.constants import (
    CODE_BASE, STACK_TOP,
    HEAP_START, HEAP_END, HEAP_SIZE,
    MCB_SIZE_OFF, MCB_FLAGS_OFF, MCB_MAGIC_OFF, MCB_HDR_SIZE,
    MCB_MAGIC, MCB_FLAG_USED, MCB_OWNER_SHIFT, MCB_MIN_BLOCK,
    MM_ALLOC_ENTRY, MM_FREE_ENTRY, MM_AVAIL_ENTRY,
    MM_INFO_ENTRY, MM_INIT_ENTRY,
)


# Accumulate all executed addresses across tests for coverage
_all_executed: set[int] = set()
_all_edges: set[tuple[int, int]] = set()
_binary_size: int = 0
_code_base: int = 0
_binary_path: Path | None = None


@pytest.fixture(scope="module")
def mm_bin():
    """Assemble the MM stub once per module."""
    return assemble_stub("stub_mm")


@pytest.fixture
def mm(mm_bin):
    """Create a fresh emulator with MM stub loaded and heap initialized."""
    global _binary_size, _code_base, _binary_path
    emu = MiniOSEmulator()
    emu.load(mm_bin)
    _binary_size = emu.code_size
    _code_base = emu.code_base
    _binary_path = mm_bin
    # Run the init entry to set up the heap
    emu.run(MM_INIT_ENTRY)
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)
    return emu


def _alloc(emu, size, owner=0):
    """Helper: call mm_alloc with CX=size, DL=owner. Returns (bx, cf)."""
    emu.set_reg("cx", size)
    emu.set_reg("dx", owner & 0xFF)
    emu.run(MM_ALLOC_ENTRY)
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)
    return emu.reg("bx"), emu.cf


def _free(emu, ptr):
    """Helper: call mm_free with BX=ptr. Returns cf."""
    emu.set_reg("bx", ptr)
    emu.run(MM_FREE_ENTRY)
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)
    return emu.cf


def _avail(emu):
    """Helper: call mm_avail. Returns (largest, total)."""
    emu.run(MM_AVAIL_ENTRY)
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)
    return emu.reg("ax"), emu.reg("dx")


def _info(emu):
    """Helper: call mm_info. Returns (total, used, free, blocks)."""
    emu.run(MM_INFO_ENTRY)
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)
    return (
        emu.reg("ax"),
        emu.reg("bx"),
        emu.reg("cx"),
        emu.reg("dx"),
    )


class TestMmInit:
    """Tests for heap initialization."""

    def test_init_creates_single_free_block(self, mm):
        """After init, heap has one free MCB spanning entire heap."""
        size = mm.read_word(HEAP_START + MCB_SIZE_OFF)
        flags = mm.read_byte(HEAP_START + MCB_FLAGS_OFF)
        magic = mm.read_byte(HEAP_START + MCB_MAGIC_OFF)

        assert size == HEAP_SIZE
        assert flags == 0x00  # free
        assert magic == MCB_MAGIC

    def test_init_avail_returns_full_heap(self, mm):
        """mm_avail after init should report almost-full heap (minus header)."""
        largest, total = _avail(mm)
        assert largest == HEAP_SIZE - MCB_HDR_SIZE
        assert total == HEAP_SIZE - MCB_HDR_SIZE

    def test_init_info_one_block(self, mm):
        """mm_info after init: 1 block, all free."""
        total, used, free, blocks = _info(mm)
        assert total == HEAP_SIZE
        assert used == 0
        assert free == HEAP_SIZE
        assert blocks == 1


class TestMmAlloc:
    """Tests for mm_alloc."""

    def test_simple_alloc(self, mm):
        """Allocate a small block, get valid pointer."""
        ptr, cf = _alloc(mm, 16)
        assert cf == False
        assert ptr == HEAP_START + MCB_HDR_SIZE

    def test_alloc_sets_used_flag(self, mm):
        """Allocated block has MCB_FLAG_USED set."""
        ptr, _ = _alloc(mm, 16)
        mcb = ptr - MCB_HDR_SIZE
        flags = mm.read_byte(mcb + MCB_FLAGS_OFF)
        assert flags & MCB_FLAG_USED != 0

    def test_alloc_preserves_magic(self, mm):
        """Allocated block retains MCB_MAGIC."""
        ptr, _ = _alloc(mm, 16)
        mcb = ptr - MCB_HDR_SIZE
        assert mm.read_byte(mcb + MCB_MAGIC_OFF) == MCB_MAGIC

    def test_alloc_splits_block(self, mm):
        """Allocating less than heap creates a remainder free block."""
        ptr, _ = _alloc(mm, 16)
        mcb = ptr - MCB_HDR_SIZE
        block_size = mm.read_word(mcb + MCB_SIZE_OFF)

        # Next block should exist after this one
        next_mcb = mcb + block_size
        assert mm.read_byte(next_mcb + MCB_MAGIC_OFF) == MCB_MAGIC
        assert mm.read_byte(next_mcb + MCB_FLAGS_OFF) == 0x00  # free

    def test_alloc_word_alignment(self, mm):
        """Odd-sized request is rounded up to even."""
        ptr, _ = _alloc(mm, 7)  # 7 bytes requested
        mcb = ptr - MCB_HDR_SIZE
        block_size = mm.read_word(mcb + MCB_SIZE_OFF)
        # 7 rounded up = 8, plus 4 header = 12
        assert block_size == 12

    def test_alloc_zero_fails(self, mm):
        """Allocating 0 bytes returns CF set."""
        _, cf = _alloc(mm, 0)
        assert cf == True

    def test_alloc_too_large_fails(self, mm):
        """Requesting more than heap fails."""
        _, cf = _alloc(mm, HEAP_SIZE + 100)
        assert cf == True

    def test_alloc_exact_heap_minus_header(self, mm):
        """Allocating exactly HEAP_SIZE - HDR_SIZE should succeed."""
        # The max usable is HEAP_SIZE - MCB_HDR_SIZE (4092 bytes)
        # But word-aligned: 4092 is even, so block = 4092 + 4 = 4096 = HEAP_SIZE
        ptr, cf = _alloc(mm, HEAP_SIZE - MCB_HDR_SIZE)
        assert cf == False
        assert ptr == HEAP_START + MCB_HDR_SIZE

    def test_multiple_allocs(self, mm):
        """Multiple allocations return sequential pointers."""
        ptr1, cf1 = _alloc(mm, 32)
        ptr2, cf2 = _alloc(mm, 64)
        assert cf1 == False
        assert cf2 == False
        assert ptr2 > ptr1

    def test_alloc_fills_heap(self, mm):
        """Allocations eventually exhaust the heap."""
        count = 0
        while True:
            _, cf = _alloc(mm, 100)
            if cf:
                break
            count += 1
            if count > 100:
                pytest.fail("Alloc never fails — infinite loop?")
        assert count > 0

    def test_alloc_owner_stored(self, mm):
        """Owner ID is stored in MCB flags bits 1-3."""
        ptr, _ = _alloc(mm, 16, owner=5)
        mcb = ptr - MCB_HDR_SIZE
        flags = mm.read_byte(mcb + MCB_FLAGS_OFF)
        owner = (flags >> MCB_OWNER_SHIFT) & 0x07
        assert owner == 5


class TestMmFree:
    """Tests for mm_free."""

    def test_free_marks_block_free(self, mm):
        """Freed block has flags=0 (free)."""
        ptr, _ = _alloc(mm, 32)
        cf = _free(mm, ptr)
        assert cf == False
        mcb = ptr - MCB_HDR_SIZE
        assert mm.read_byte(mcb + MCB_FLAGS_OFF) == 0x00

    def test_free_invalid_pointer_below_heap(self, mm):
        """Freeing a pointer below heap returns CF."""
        cf = _free(mm, HEAP_START)  # Points to MCB header, not payload
        assert cf == True

    def test_free_invalid_pointer_above_heap(self, mm):
        """Freeing a pointer above heap returns CF."""
        cf = _free(mm, HEAP_END + 100)
        assert cf == True

    def test_double_free_fails(self, mm):
        """Freeing an already-free block returns CF."""
        ptr, _ = _alloc(mm, 32)
        _free(mm, ptr)
        cf = _free(mm, ptr)
        assert cf == True

    def test_free_coalesces_adjacent(self, mm):
        """Freeing a block merges with adjacent free block."""
        ptr1, _ = _alloc(mm, 32)
        ptr2, _ = _alloc(mm, 32)
        _free(mm, ptr2)  # Free second block
        _free(mm, ptr1)  # Free first — should coalesce with second

        # After coalesce, first MCB should span both blocks + remainder
        mcb1 = ptr1 - MCB_HDR_SIZE
        size = mm.read_word(mcb1 + MCB_SIZE_OFF)
        # Should be the entire heap again (all coalesced back)
        assert size == HEAP_SIZE

    def test_free_coalesces_chain(self, mm):
        """Three adjacent frees coalesce into one block."""
        ptr1, _ = _alloc(mm, 16)
        ptr2, _ = _alloc(mm, 16)
        ptr3, _ = _alloc(mm, 16)

        # Free in order: 3, 2, 1 — each should trigger forward coalesce
        _free(mm, ptr3)
        _free(mm, ptr2)
        _free(mm, ptr1)

        # Should all merge back to one block
        size = mm.read_word(HEAP_START + MCB_SIZE_OFF)
        assert size == HEAP_SIZE

    def test_free_no_backward_coalesce(self, mm):
        """Freeing doesn't merge with previous free block (no backward coalesce)."""
        ptr1, _ = _alloc(mm, 32)
        ptr2, _ = _alloc(mm, 32)
        ptr3, _ = _alloc(mm, 32)

        _free(mm, ptr1)  # Free first
        _free(mm, ptr3)  # Free third (second still allocated — no merge)

        # First block shouldn't have grown (second is in the way)
        mcb1 = ptr1 - MCB_HDR_SIZE
        size1 = mm.read_word(mcb1 + MCB_SIZE_OFF)
        # Should be just the first block's size (36 = 32 rounded + 4 header)
        assert size1 < HEAP_SIZE


class TestMmAvail:
    """Tests for mm_avail."""

    def test_avail_after_alloc(self, mm):
        """Available decreases after allocation."""
        initial_largest, initial_total = _avail(mm)
        _alloc(mm, 100)
        largest, total = _avail(mm)
        assert total < initial_total
        assert largest < initial_largest

    def test_avail_after_free(self, mm):
        """Available increases after free."""
        ptr, _ = _alloc(mm, 100)
        _, total_after_alloc = _avail(mm)
        _free(mm, ptr)
        _, total_after_free = _avail(mm)
        assert total_after_free > total_after_alloc

    def test_avail_fragmented(self, mm):
        """Largest != total when heap is fragmented."""
        ptr1, _ = _alloc(mm, 100)
        ptr2, _ = _alloc(mm, 100)
        ptr3, _ = _alloc(mm, 100)
        _free(mm, ptr1)
        _free(mm, ptr3)  # Two separate free regions

        largest, total = _avail(mm)
        # Total > largest because memory is split into non-adjacent free blocks
        assert total > largest


class TestMmInfo:
    """Tests for mm_info."""

    def test_info_after_alloc(self, mm):
        """Info reports correct used/free after allocation."""
        _alloc(mm, 100)
        total, used, free, blocks = _info(mm)
        assert total == HEAP_SIZE
        assert used > 0
        assert free > 0
        assert used + free == total
        assert blocks == 2  # allocated block + remainder free block

    def test_info_multiple_allocs(self, mm):
        """Block count increases with allocations."""
        _alloc(mm, 32)
        _alloc(mm, 32)
        _alloc(mm, 32)
        _, _, _, blocks = _info(mm)
        assert blocks == 4  # 3 allocated + 1 remainder

    def test_info_after_free_coalesce(self, mm):
        """After alloc+free cycle, back to 1 block."""
        ptr, _ = _alloc(mm, 32)
        _free(mm, ptr)
        total, used, free, blocks = _info(mm)
        assert used == 0
        assert free == HEAP_SIZE
        assert blocks == 1


# ─── Coverage registration (runs after all tests in this module) ──────────────

@pytest.fixture(autouse=True, scope="module")
def _register_coverage_after_all():
    yield
    if _binary_size > 0:
        in_binary = {a for a in _all_executed if _code_base <= a < _code_base + _binary_size}
        register_coverage("mm_allocator", _binary_size, len(in_binary),
                          edges=_all_edges, binary_path=_binary_path)
