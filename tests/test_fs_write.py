"""Unit tests for FS write/delete/rename operations.

Tests the MNFS v1.1 write support: creating files, deleting files (tombstones),
renaming files, and verifying directory integrity — all via Unicorn Engine
emulation with a virtual disk buffer.
"""

import struct
import pytest
from pathlib import Path
from tests.harness.emulator import MiniOSEmulator
from tests.harness.assembler import assemble_stub
from tests.conftest import register_coverage
from tests.harness.constants import (
    CODE_BASE,
    MNFS_DIR_SECTOR, MNFS_DIR_SECTORS,
    MNFS_HDR_SIZE, MNFS_ENTRY_SIZE, MNFS_MAX_ENTRIES,
    MNFS_NAME_LEN, MNFS_MAGIC,
    MNFS_HDR_COUNT, MNFS_HDR_TOTAL, MNFS_HDR_CAPACITY,
    MNFS_HDR_VERSION,
    MNFS_ENT_ATTR, MNFS_ENT_START, MNFS_ENT_SECTORS, MNFS_ENT_BYTES,
    MNFS_ATTR_SYSTEM, MNFS_ATTR_EXEC,
    MNFS_DELETED,
    FS_ERR_NOT_FOUND, FS_ERR_EXISTS, FS_ERR_DIR_FULL,
    FS_ERR_DISK_FULL, FS_ERR_IO, FS_ERR_PROTECTED,
)

# Entry point offsets from CODE_BASE (passed to emu.run())
FS_WRITE_ENTRY = 0x00
FS_DELETE_ENTRY = 0x20
FS_RENAME_ENTRY = 0x40
FS_FIND_ENTRY = 0x60
FS_INIT_DIR_ENTRY = 0x80
FS_GET_DIR_CACHE_ENTRY = 0xA0

# Virtual disk base (must match stub)
VDISK_BASE = 0x4000
VDISK_DIR_OFF = MNFS_DIR_SECTOR * 512  # 1024
VDISK_DATA_OFF = (MNFS_DIR_SECTOR + MNFS_DIR_SECTORS) * 512  # 1536

# Coverage tracking
_all_executed: set[int] = set()
_all_edges: set[tuple[int, int]] = set()
_binary_size: int = 0
_binary_path: Path | None = None


def _make_name(name: str, ext: str = "   ") -> bytes:
    """Create an 11-byte 8.3 filename."""
    return name.ljust(8).encode('ascii')[:8] + ext.ljust(3).encode('ascii')[:3]


def _make_dir_sector(files: list[dict], capacity: int = 30000) -> bytes:
    """Build a 512-byte MNFS directory sector from a file list.

    Each file dict: {name: str, ext: str, attr: int, start: int, sectors: int, bytes: int}
    """
    # Header (32 bytes)
    hdr = bytearray(32)
    hdr[0:4] = b'MNFS'
    hdr[4] = 0x01  # version
    hdr[5] = len(files)  # file count
    # total_sectors = sum of all file sectors + 1 (directory)
    total = MNFS_DIR_SECTORS + sum(f.get('sectors', 0) for f in files)
    struct.pack_into('<H', hdr, 6, total)
    struct.pack_into('<H', hdr, 8, capacity)

    # Entries (32 bytes each)
    entries = bytearray(480)  # 15 * 32
    for i, f in enumerate(files):
        off = i * 32
        name_bytes = _make_name(f.get('name', ''), f.get('ext', ''))
        entries[off:off+11] = name_bytes
        entries[off+11] = f.get('attr', 0)
        struct.pack_into('<I', entries, off+12, f.get('start', 0))
        struct.pack_into('<H', entries, off+16, f.get('sectors', 0))
        struct.pack_into('<I', entries, off+18, f.get('bytes', 0))

    return bytes(hdr + entries)


@pytest.fixture(scope="module")
def fs_bin():
    """Assemble the FS write stub once per module."""
    return assemble_stub("stub_fs_write")


@pytest.fixture
def fs(fs_bin):
    """Create a fresh emulator with FS stub loaded and empty directory."""
    global _binary_size, _binary_path
    emu = MiniOSEmulator()
    emu.load(fs_bin)
    _binary_size = emu.code_size
    _binary_path = fs_bin

    # Set up empty MNFS directory on virtual disk
    empty_dir = _make_dir_sector([], capacity=100)
    emu.write_bytes(VDISK_BASE + VDISK_DIR_OFF, empty_dir)

    # Initialize dir_cache from vdisk
    emu.run(FS_INIT_DIR_ENTRY)
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)
    return emu


@pytest.fixture
def fs_with_files(fs_bin):
    """Emulator with 3 pre-existing files in directory."""
    global _binary_size, _binary_path
    emu = MiniOSEmulator()
    emu.load(fs_bin)
    _binary_size = emu.code_size
    _binary_path = fs_bin

    files = [
        {'name': 'LOADER', 'ext': 'SYS', 'attr': MNFS_ATTR_SYSTEM, 'start': 3, 'sectors': 3, 'bytes': 1536},
        {'name': 'KERNEL', 'ext': 'SYS', 'attr': MNFS_ATTR_SYSTEM, 'start': 6, 'sectors': 8, 'bytes': 4096},
        {'name': 'HELLO', 'ext': 'MNX', 'attr': MNFS_ATTR_EXEC, 'start': 14, 'sectors': 1, 'bytes': 512},
    ]
    dir_sector = _make_dir_sector(files, capacity=100)
    emu.write_bytes(VDISK_BASE + VDISK_DIR_OFF, dir_sector)

    emu.run(FS_INIT_DIR_ENTRY)
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)
    return emu


def _write_file(emu, name: bytes, data: bytes, attr: int = 0):
    """Helper: call fs_write_impl."""
    # Place name at 0x3000, data at 0x3100
    name_addr = 0x3000
    data_addr = 0x3100
    emu.write_bytes(name_addr, name)
    if data:
        emu.write_bytes(data_addr, data)

    emu.set_reg("si", name_addr)
    emu.set_reg("bx", data_addr)
    emu.set_reg("es", 0)
    emu.set_reg("ecx", len(data))
    emu.set_reg("dx", attr)
    emu.run(FS_WRITE_ENTRY)
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)
    return emu.cf, emu.reg("ax") & 0xFF


def _delete_file(emu, name: bytes):
    """Helper: call fs_delete_impl."""
    name_addr = 0x3000
    emu.write_bytes(name_addr, name)
    emu.set_reg("si", name_addr)
    emu.run(FS_DELETE_ENTRY)
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)
    return emu.cf, emu.reg("ax") & 0xFF


def _rename_file(emu, old_name: bytes, new_name: bytes):
    """Helper: call fs_rename_impl."""
    old_addr = 0x3000
    new_addr = 0x3020
    emu.write_bytes(old_addr, old_name)
    emu.write_bytes(new_addr, new_name)
    emu.set_reg("si", old_addr)
    emu.set_reg("di", new_addr)
    emu.set_reg("es", 0)
    emu.run(FS_RENAME_ENTRY)
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)
    return emu.cf, emu.reg("ax") & 0xFF


def _find_file(emu, name: bytes):
    """Helper: call fs_find_impl. Returns (found, start, sectors, bytes, attr)."""
    name_addr = 0x3000
    emu.write_bytes(name_addr, name)
    emu.set_reg("si", name_addr)
    emu.run(FS_FIND_ENTRY)
    _all_executed.update(emu.coverage_in_binary)
    _all_edges.update(emu.edges_in_binary)
    if emu.cf:
        return False, 0, 0, 0, 0
    return True, emu.reg("eax"), emu.reg("cx"), emu.reg("edx"), emu.reg("bx") & 0xFF


def _get_dir_cache(emu) -> bytes:
    """Read the 512-byte directory cache from emulator memory."""
    emu.run(FS_GET_DIR_CACHE_ENTRY)
    addr = emu.reg("bx")
    return emu.read_bytes(addr, 512)


# =============================================================================
# WRITE TESTS
# =============================================================================

class TestFsWrite:
    """Tests for FS_WRITE_FILE."""

    def test_write_new_file_empty_dir(self, fs):
        """Write a file into an empty directory."""
        name = _make_name("TEST", "TXT")
        data = b"Hello, World!"
        cf, err = _write_file(fs, name, data, attr=0)

        assert not cf, f"Write failed with error {err}"

        # Verify file can be found
        found, start, sectors, size, attr = _find_file(fs, name)
        assert found
        assert sectors == 1  # 13 bytes → 1 sector
        assert size == 13
        assert attr == 0

    def test_write_file_data_on_vdisk(self, fs):
        """Verify written data appears at correct vdisk offset."""
        name = _make_name("DATA", "BIN")
        data = b"\xAA" * 100
        cf, _ = _write_file(fs, name, data)
        assert not cf

        # File starts at sector 3 (first data sector after dir)
        # Start sector from header: total was 1 (dir) before write,
        # so append at sector MNFS_DIR_SECTOR + 1 = 3
        found, start, _, _, _ = _find_file(fs, name)
        assert found
        assert start == MNFS_DIR_SECTOR + MNFS_DIR_SECTORS  # sector 3

        # Check data on vdisk
        vdisk_off = start * 512
        written = fs.read_bytes(VDISK_BASE + vdisk_off, 100)
        assert written == data

    def test_write_multiple_files(self, fs):
        """Write multiple files sequentially."""
        # File 1: 512 bytes (1 sector)
        name1 = _make_name("FILE1", "DAT")
        data1 = b"\x01" * 512
        cf, _ = _write_file(fs, name1, data1)
        assert not cf

        # File 2: 1024 bytes (2 sectors)
        name2 = _make_name("FILE2", "DAT")
        data2 = b"\x02" * 1024
        cf, _ = _write_file(fs, name2, data2)
        assert not cf

        # Verify both exist and are contiguous
        found1, start1, sec1, _, _ = _find_file(fs, name1)
        found2, start2, sec2, _, _ = _find_file(fs, name2)
        assert found1 and found2
        assert sec1 == 1
        assert sec2 == 2
        assert start2 == start1 + sec1  # Contiguous

    def test_write_zero_length_file(self, fs):
        """Write a zero-length file (directory entry only)."""
        name = _make_name("EMPTY", "   ")
        cf, _ = _write_file(fs, name, b"")
        assert not cf

        found, start, sectors, size, _ = _find_file(fs, name)
        assert found
        assert sectors == 0
        assert size == 0

    def test_write_boundary_sizes(self, fs):
        """Test file sizes at sector boundaries."""
        # 1 byte → 1 sector
        cf, _ = _write_file(fs, _make_name("S1BYTE"), b"\xFF")
        assert not cf
        _, _, sec, sz, _ = _find_file(fs, _make_name("S1BYTE"))
        assert sec == 1 and sz == 1

        # 512 bytes → 1 sector
        cf, _ = _write_file(fs, _make_name("S512B"), b"\xAA" * 512)
        assert not cf
        _, _, sec, sz, _ = _find_file(fs, _make_name("S512B"))
        assert sec == 1 and sz == 512

        # 513 bytes → 2 sectors
        cf, _ = _write_file(fs, _make_name("S513B"), b"\xBB" * 513)
        assert not cf
        _, _, sec, sz, _ = _find_file(fs, _make_name("S513B"))
        assert sec == 2 and sz == 513

    def test_write_duplicate_name_fails(self, fs):
        """Writing a file with existing name returns ERR_EXISTS."""
        name = _make_name("DUPE", "TXT")
        cf, _ = _write_file(fs, name, b"first")
        assert not cf

        cf, err = _write_file(fs, name, b"second")
        assert cf
        assert err == FS_ERR_EXISTS

    def test_write_directory_full(self, fs):
        """Filling all 15 slots, then writing 16th fails."""
        for i in range(15):
            name = _make_name(f"F{i:06d}")
            cf, err = _write_file(fs, name, b"x")
            assert not cf, f"File {i} failed: err={err}"

        # 16th file should fail
        cf, err = _write_file(fs, _make_name("TOOMANY"), b"x")
        assert cf
        assert err == FS_ERR_DIR_FULL

    def test_write_disk_full(self, fs_bin):
        """Writing when disk capacity is exceeded fails."""
        emu = MiniOSEmulator()
        emu.load(fs_bin)

        # Create dir with very small capacity (5 sectors total)
        dir_sector = _make_dir_sector([], capacity=5)
        emu.write_bytes(VDISK_BASE + VDISK_DIR_OFF, dir_sector)
        emu.run(FS_INIT_DIR_ENTRY)

        # Try to write 3 sectors (3*512=1536 bytes) + 1 dir = 4 data sectors needed
        # Capacity is 5, directory uses 1, so 4 data sectors available
        # total_sectors (header) starts at 1. After write: 1 + 3 = 4. 4 + 1(dir) = 5 ≤ 5 → OK
        name = _make_name("BIG", "DAT")
        data = b"\xFF" * (3 * 512)
        cf, _ = _write_file(emu, name, data)
        assert not cf  # Should fit

        # Now write another 2 sectors — would exceed capacity
        name2 = _make_name("TOO", "BIG")
        data2 = b"\xEE" * (2 * 512)
        cf, err = _write_file(emu, name2, data2)
        assert cf
        assert err == FS_ERR_DISK_FULL

    def test_write_invalid_name_null(self, fs):
        """Writing with null first byte fails."""
        name = b"\x00" + b"TEST   TXT"
        cf, err = _write_file(fs, name, b"data")
        assert cf

    def test_write_invalid_name_deleted_marker(self, fs):
        """Writing with 0xE5 first byte fails."""
        name = bytes([MNFS_DELETED]) + b"TEST   TXT"
        cf, err = _write_file(fs, name, b"data")
        assert cf

    def test_write_with_attribute(self, fs):
        """Written file preserves attribute byte."""
        name = _make_name("PROG", "MNX")
        cf, _ = _write_file(fs, name, b"\x90" * 512, attr=MNFS_ATTR_EXEC)
        assert not cf

        found, _, _, _, attr = _find_file(fs, name)
        assert found
        assert attr == MNFS_ATTR_EXEC


# =============================================================================
# DELETE TESTS
# =============================================================================

class TestFsDelete:
    """Tests for FS_DELETE_FILE."""

    def test_delete_existing_file(self, fs_with_files):
        """Delete HELLO.MNX (non-system) succeeds."""
        name = _make_name("HELLO", "MNX")
        cf, _ = _delete_file(fs_with_files, name)
        assert not cf

        # File should no longer be findable
        found, _, _, _, _ = _find_file(fs_with_files, name)
        assert not found

    def test_delete_reclaims_trailing_space(self, fs_with_files):
        """Deleting the last physical file reduces total_sectors."""
        emu = fs_with_files

        # HELLO.MNX is the last file (start=14, sectors=1, so end=15)
        # Before delete: total = 1(dir) + 3+8+1 = 13
        dir_before = _get_dir_cache(emu)
        total_before = struct.unpack_from('<H', dir_before, MNFS_HDR_TOTAL)[0]

        name = _make_name("HELLO", "MNX")
        cf, _ = _delete_file(emu, name)
        assert not cf

        # After delete: high-water = max(3+3, 6+8) - 2 = 14 - 2 = 12
        dir_after = _get_dir_cache(emu)
        total_after = struct.unpack_from('<H', dir_after, MNFS_HDR_TOTAL)[0]
        assert total_after < total_before

    def test_delete_system_file_fails(self, fs_with_files):
        """Cannot delete a file with SYSTEM attribute."""
        name = _make_name("LOADER", "SYS")
        cf, err = _delete_file(fs_with_files, name)
        assert cf
        assert err == FS_ERR_PROTECTED

    def test_delete_nonexistent_fails(self, fs_with_files):
        """Deleting non-existent file returns ERR_NOT_FOUND."""
        name = _make_name("NOFILE", "TXT")
        cf, err = _delete_file(fs_with_files, name)
        assert cf
        assert err == FS_ERR_NOT_FOUND

    def test_delete_middle_file_leaves_tombstone(self, fs_bin):
        """Deleting a middle file marks it as tombstone but doesn't compact."""
        emu = MiniOSEmulator()
        emu.load(fs_bin)

        files = [
            {'name': 'FILE1', 'ext': 'DAT', 'attr': 0, 'start': 3, 'sectors': 2, 'bytes': 1024},
            {'name': 'FILE2', 'ext': 'DAT', 'attr': 0, 'start': 5, 'sectors': 2, 'bytes': 1024},
            {'name': 'FILE3', 'ext': 'DAT', 'attr': 0, 'start': 7, 'sectors': 2, 'bytes': 1024},
        ]
        dir_sector = _make_dir_sector(files, capacity=100)
        emu.write_bytes(VDISK_BASE + VDISK_DIR_OFF, dir_sector)
        emu.run(FS_INIT_DIR_ENTRY)

        # Delete FILE2 (middle)
        name = _make_name("FILE2", "DAT")
        cf, _ = _delete_file(emu, name)
        assert not cf

        # FILE2 not findable
        found, _, _, _, _ = _find_file(emu, name)
        assert not found

        # But FILE1 and FILE3 still exist
        found1, _, _, _, _ = _find_file(emu, _make_name("FILE1", "DAT"))
        found3, _, _, _, _ = _find_file(emu, _make_name("FILE3", "DAT"))
        assert found1 and found3

        # total_sectors should still reflect FILE3's end (high-water)
        dir_data = _get_dir_cache(emu)
        total = struct.unpack_from('<H', dir_data, MNFS_HDR_TOTAL)[0]
        # FILE3 ends at sector 9, high-water = 9 - 2 = 7
        assert total == 7

    def test_write_reuses_deleted_slot(self, fs_bin):
        """Writing after a delete reuses the tombstone directory slot."""
        emu = MiniOSEmulator()
        emu.load(fs_bin)

        files = [
            {'name': 'FIRST', 'ext': 'DAT', 'attr': 0, 'start': 3, 'sectors': 1, 'bytes': 512},
            {'name': 'SECOND', 'ext': 'DAT', 'attr': 0, 'start': 4, 'sectors': 1, 'bytes': 512},
        ]
        dir_sector = _make_dir_sector(files, capacity=100)
        emu.write_bytes(VDISK_BASE + VDISK_DIR_OFF, dir_sector)
        emu.run(FS_INIT_DIR_ENTRY)

        # Delete FIRST
        cf, _ = _delete_file(emu, _make_name("FIRST", "DAT"))
        assert not cf

        # Write a new file — should reuse FIRST's directory slot
        new_name = _make_name("NEWFILE", "BIN")
        cf, _ = _write_file(emu, new_name, b"\x42" * 256)
        assert not cf

        # Verify new file exists
        found, _, _, _, _ = _find_file(emu, new_name)
        assert found

        # File count should be 2 (SECOND + NEWFILE)
        dir_data = _get_dir_cache(emu)
        count = dir_data[MNFS_HDR_COUNT]
        assert count == 2


# =============================================================================
# RENAME TESTS
# =============================================================================

class TestFsRename:
    """Tests for FS_RENAME_FILE."""

    def test_rename_success(self, fs_with_files):
        """Rename HELLO.MNX to WORLD.MNX."""
        old_name = _make_name("HELLO", "MNX")
        new_name = _make_name("WORLD", "MNX")
        cf, _ = _rename_file(fs_with_files, old_name, new_name)
        assert not cf

        # Old name gone, new name present
        found_old, _, _, _, _ = _find_file(fs_with_files, old_name)
        found_new, start, sectors, size, attr = _find_file(fs_with_files, new_name)
        assert not found_old
        assert found_new
        assert sectors == 1
        assert size == 512
        assert attr == MNFS_ATTR_EXEC

    def test_rename_nonexistent_fails(self, fs_with_files):
        """Renaming non-existent file returns ERR_NOT_FOUND."""
        old_name = _make_name("NOFILE", "TXT")
        new_name = _make_name("NEW", "TXT")
        cf, err = _rename_file(fs_with_files, old_name, new_name)
        assert cf
        assert err == FS_ERR_NOT_FOUND

    def test_rename_to_existing_fails(self, fs_with_files):
        """Renaming to an existing name returns ERR_EXISTS."""
        old_name = _make_name("HELLO", "MNX")
        new_name = _make_name("KERNEL", "SYS")  # Already exists
        cf, err = _rename_file(fs_with_files, old_name, new_name)
        assert cf
        assert err == FS_ERR_EXISTS

    def test_rename_preserves_file_data(self, fs_with_files):
        """Rename doesn't change start sector or size."""
        old_name = _make_name("HELLO", "MNX")
        # Get original metadata
        found, orig_start, orig_sec, orig_bytes, orig_attr = _find_file(
            fs_with_files, old_name)
        assert found

        new_name = _make_name("RENAMED", "MNX")
        cf, _ = _rename_file(fs_with_files, old_name, new_name)
        assert not cf

        # Verify metadata unchanged
        found, start, sec, nbytes, attr = _find_file(fs_with_files, new_name)
        assert found
        assert start == orig_start
        assert sec == orig_sec
        assert nbytes == orig_bytes
        assert attr == orig_attr

    def test_rename_invalid_new_name_null(self, fs_with_files):
        """Rename with null new name first byte fails."""
        old_name = _make_name("HELLO", "MNX")
        new_name = b"\x00NEWNAME MNX"
        cf, err = _rename_file(fs_with_files, old_name, new_name)
        assert cf

    def test_rename_invalid_new_name_deleted(self, fs_with_files):
        """Rename with 0xE5 new name first byte fails."""
        old_name = _make_name("HELLO", "MNX")
        new_name = bytes([MNFS_DELETED]) + b"NEWNAME MNX"
        cf, err = _rename_file(fs_with_files, old_name, new_name)
        assert cf


# =============================================================================
# FIND TESTS (tombstone skipping)
# =============================================================================

class TestFsFind:
    """Tests that FIND skips tombstoned entries."""

    def test_find_skips_deleted(self, fs_bin):
        """FIND doesn't return a deleted entry."""
        emu = MiniOSEmulator()
        emu.load(fs_bin)

        files = [
            {'name': 'ALIVE', 'ext': 'TXT', 'attr': 0, 'start': 3, 'sectors': 1, 'bytes': 100},
        ]
        dir_sector = bytearray(_make_dir_sector(files, capacity=100))
        # Manually add a tombstoned entry at slot 1
        off = MNFS_HDR_SIZE + MNFS_ENTRY_SIZE
        dir_sector[off] = MNFS_DELETED  # Mark as deleted
        dir_sector[off+1:off+11] = b"DEAD   TXT"

        emu.write_bytes(VDISK_BASE + VDISK_DIR_OFF, bytes(dir_sector))
        emu.run(FS_INIT_DIR_ENTRY)

        # ALIVE should be found
        found, _, _, _, _ = _find_file(emu, _make_name("ALIVE", "TXT"))
        assert found

        # DEAD should NOT be found (tombstoned)
        found, _, _, _, _ = _find_file(emu, _make_name("DEAD", "TXT"))
        assert not found


# =============================================================================
# INTEGRATION TESTS (write + delete + write cycle)
# =============================================================================

class TestFsIntegration:
    """End-to-end scenarios combining write, delete, and rename."""

    def test_write_delete_write_cycle(self, fs):
        """Write → Delete → Write reuses slot and appends data correctly."""
        # Write file A
        name_a = _make_name("FILEA", "DAT")
        cf, _ = _write_file(fs, name_a, b"A" * 512)
        assert not cf

        # Write file B
        name_b = _make_name("FILEB", "DAT")
        cf, _ = _write_file(fs, name_b, b"B" * 512)
        assert not cf

        # Delete file A
        cf, _ = _delete_file(fs, name_a)
        assert not cf

        # Write file C — should reuse A's slot, append to end of disk
        name_c = _make_name("FILEC", "DAT")
        cf, _ = _write_file(fs, name_c, b"C" * 512)
        assert not cf

        # Verify: B and C exist, A doesn't
        assert not _find_file(fs, name_a)[0]
        assert _find_file(fs, name_b)[0]
        found_c, start_c, _, _, _ = _find_file(fs, name_c)
        assert found_c

        # C should start AFTER B (append, not reuse A's disk space)
        _, start_b, sec_b, _, _ = _find_file(fs, name_b)
        assert start_c == start_b + sec_b

    def test_rename_then_find_old_fails(self, fs):
        """After rename, old name is gone."""
        name = _make_name("ORIG", "TXT")
        cf, _ = _write_file(fs, name, b"data")
        assert not cf

        new_name = _make_name("RENAMED", "TXT")
        cf, _ = _rename_file(fs, name, new_name)
        assert not cf

        assert not _find_file(fs, name)[0]
        assert _find_file(fs, new_name)[0]


# =============================================================================
# Coverage registration
# =============================================================================

def teardown_module():
    """Register coverage data after all tests in this module."""
    if _binary_size > 0:
        register_coverage(
            "fs_write",
            _binary_size,
            len(_all_executed),
            edges=_all_edges,
            binary_path=_binary_path,
        )
