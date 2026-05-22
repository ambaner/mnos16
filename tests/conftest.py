"""pytest configuration and shared fixtures for MNOS16 unit tests."""

import pytest
from pathlib import Path

from tests.harness.assembler import assemble_stub
from tests.harness.emulator import MiniOSEmulator
from tests.harness.coverage import generate_report, print_summary


# ─── Global coverage tracking ─────────────────────────────────────────────────
# Each test module registers its coverage data here.
_coverage_data: dict[str, dict] = {}


def register_coverage(routine_name: str, total_addrs: int, hit_addrs: int,
                      edges: set[tuple[int, int]] | None = None,
                      binary_path: str | Path | None = None):
    """Register coverage data for a routine (called by test modules).

    Args:
        routine_name: Name of the routine being tested.
        total_addrs: Total instruction addresses in the binary.
        hit_addrs: Number of addresses actually executed.
        edges: Optional set of (from, to) edges for branch coverage.
        binary_path: Optional path to the binary for branch analysis.
    """
    pct = (hit_addrs / total_addrs * 100) if total_addrs > 0 else 0
    _coverage_data[routine_name] = {
        "total_addrs": total_addrs,
        "hit_addrs": hit_addrs,
        "percentage": pct,
        "edges": edges,
        "binary_path": binary_path,
    }


def pytest_sessionfinish(session, exitstatus):
    """Generate coverage report after all tests complete."""
    if _coverage_data:
        project_root = Path(__file__).resolve().parent.parent
        output_dir = project_root / "coverage"
        summary = generate_report(_coverage_data, output_dir)
        print_summary(summary)


# ─── Fixtures ─────────────────────────────────────────────────────────────────

@pytest.fixture
def emu():
    """Fresh emulator instance."""
    return MiniOSEmulator()


@pytest.fixture(scope="module")
def parse_args_bin():
    """Assembled shell_parse_args stub binary."""
    return assemble_stub("stub_parse_args")


@pytest.fixture(scope="module")
def strcmp_bin():
    """Assembled strcmp stub binary."""
    return assemble_stub("stub_strcmp")


@pytest.fixture(scope="module")
def parse_fname_bin():
    """Assembled run_parse_filename stub binary."""
    return assemble_stub("stub_parse_fname")


@pytest.fixture(scope="module")
def cmdmatch_bin():
    """Assembled cmdmatch stub binary."""
    return assemble_stub("stub_cmdmatch")


@pytest.fixture(scope="module")
def edit_gap_bin():
    """Assembled edit gap buffer stub binary."""
    return assemble_stub("stub_edit_gap")


@pytest.fixture(scope="module")
def edit_find_bin():
    """Assembled edit find/search stub binary."""
    return assemble_stub("stub_edit_find")


@pytest.fixture(scope="module")
def edit_fname_bin():
    """Assembled edit filename parser stub binary."""
    return assemble_stub("stub_edit_fname")
