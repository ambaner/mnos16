"""Coverage collector and reporter for MNOS16 unit tests.

Generates:
  - coverage/index.html    — HTML coverage report with branch coverage
  - coverage/summary.json  — machine-readable summary
  - coverage/badge.json    — shields.io endpoint for README badge
  - coverage/history.json  — append-only trend data (last 50 runs)
  - coverage/trend.html    — Chart.js trend visualization
"""

import json
import sys
from pathlib import Path
from datetime import datetime, timezone


def generate_report(
    results: dict[str, dict],
    output_dir: str | Path = "coverage",
):
    """Generate coverage report files.

    Args:
        results: Dict of {routine_name: {total_addrs, hit_addrs, percentage, edges, binary_path}}
        output_dir: Directory to write reports to.
    """
    from tests.harness.branch_coverage import find_branches, analyze_branch_coverage
    from tests.harness.constants import CODE_BASE

    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)

    # Calculate overall statement coverage stats
    total_addrs = sum(r.get("total_addrs", 0) for r in results.values())
    hit_addrs = sum(r.get("hit_addrs", 0) for r in results.values())
    overall_pct = (hit_addrs / total_addrs * 100) if total_addrs > 0 else 0

    # Calculate branch coverage per routine
    branch_results = {}
    total_outcomes = 0
    covered_outcomes = 0

    for name, r in results.items():
        edges = r.get("edges")
        binary_path = r.get("binary_path")
        if edges and binary_path and Path(binary_path).exists():
            branches = find_branches(binary_path, CODE_BASE)
            branch_info = analyze_branch_coverage(branches, edges)
            branch_results[name] = branch_info
            total_outcomes += branch_info["outcomes_total"]
            covered_outcomes += branch_info["outcomes_covered"]

    overall_branch_pct = (covered_outcomes / total_outcomes * 100) if total_outcomes > 0 else 0

    # --- JSON summary ---
    summary = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "overall_coverage": round(overall_pct, 1),
        "total_addresses": total_addrs,
        "hit_addresses": hit_addrs,
        "branch_coverage": round(overall_branch_pct, 1),
        "branch_outcomes_total": total_outcomes,
        "branch_outcomes_covered": covered_outcomes,
        "routines": {
            name: {
                "coverage": round(r.get("percentage", 0), 1),
                "total": r.get("total_addrs", 0),
                "hit": r.get("hit_addrs", 0),
                "branch_coverage": branch_results.get(name, {}).get("branch_coverage_pct", None),
                "branches_total": branch_results.get(name, {}).get("branches_total", 0),
                "branches_full": branch_results.get(name, {}).get("branches_full", 0),
            }
            for name, r in results.items()
        },
    }
    (out / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

    # --- Badge JSON (shields.io endpoint) ---
    color = "red" if overall_pct < 50 else "yellow" if overall_pct < 80 else "green"
    badge = {
        "schemaVersion": 1,
        "label": "coverage",
        "message": f"{overall_pct:.0f}% stmt | {overall_branch_pct:.0f}% branch",
        "color": color,
    }
    (out / "badge.json").write_text(json.dumps(badge, indent=2), encoding="utf-8")

    # --- Update history ---
    _update_history(out, summary)

    # --- HTML report ---
    _generate_html_report(out, results, branch_results, overall_pct, overall_branch_pct,
                          total_addrs, hit_addrs, total_outcomes, covered_outcomes)

    # --- Trend HTML ---
    _generate_trend_html(out)

    return summary


def _update_history(out: Path, summary: dict):
    """Append current run to history.json (keep last 50 entries)."""
    history_path = out / "history.json"
    history = []
    if history_path.exists():
        try:
            history = json.loads(history_path.read_text(encoding="utf-8"))
            if not isinstance(history, list):
                history = []
        except (json.JSONDecodeError, OSError):
            history = []

    entry = {
        "timestamp": summary["timestamp"],
        "statement_coverage": summary["overall_coverage"],
        "branch_coverage": summary["branch_coverage"],
        "total_addresses": summary["total_addresses"],
        "hit_addresses": summary["hit_addresses"],
        "branch_outcomes_total": summary["branch_outcomes_total"],
        "branch_outcomes_covered": summary["branch_outcomes_covered"],
        "routines": {
            name: {
                "stmt": r["coverage"],
                "branch": r.get("branch_coverage"),
            }
            for name, r in summary["routines"].items()
        },
    }
    history.append(entry)
    # Keep last 50 entries
    history = history[-50:]
    history_path.write_text(json.dumps(history, indent=2), encoding="utf-8")


def _generate_html_report(out, results, branch_results, overall_pct, overall_branch_pct,
                          total_addrs, hit_addrs, total_outcomes, covered_outcomes):
    """Generate the main coverage HTML report."""
    rows = ""
    for name, r in sorted(results.items()):
        pct = r.get("percentage", 0)
        filled = int(pct / 10)
        bar = "█" * filled + "░" * (10 - filled)

        br = branch_results.get(name, {})
        br_pct = br.get("branch_coverage_pct", None)
        br_total = br.get("branches_total", 0)
        br_full = br.get("branches_full", 0)
        if br_pct is not None:
            br_filled = int(br_pct / 10)
            br_bar = "█" * br_filled + "░" * (10 - br_filled)
            br_cell = f"<code>{br_bar}</code> {br_pct:.0f}% ({br_full}/{br_total})"
        else:
            br_cell = "—"

        rows += f"""        <tr>
            <td>{name}</td>
            <td>{r.get('total_addrs', 0)}</td>
            <td>{r.get('hit_addrs', 0)}</td>
            <td><code>{bar}</code> {pct:.0f}%</td>
            <td>{br_cell}</td>
        </tr>\n"""

    overall_filled = int(overall_pct / 10)
    overall_bar = "█" * overall_filled + "░" * (10 - overall_filled)
    branch_filled = int(overall_branch_pct / 10)
    branch_bar = "█" * branch_filled + "░" * (10 - branch_filled)

    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <title>MNOS16 Coverage Report</title>
    <style>
        body {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
               max-width: 900px; margin: 40px auto; padding: 0 20px;
               color: #24292e; background: #fff; }}
        h1 {{ border-bottom: 1px solid #e1e4e8; padding-bottom: 8px; }}
        .overall {{ font-size: 1.1em; margin: 20px 0; padding: 16px;
                    background: #f6f8fa; border-radius: 6px; }}
        .metrics {{ display: flex; gap: 32px; }}
        .metric {{ flex: 1; }}
        table {{ border-collapse: collapse; width: 100%; margin-top: 20px; }}
        th, td {{ text-align: left; padding: 8px 12px; border-bottom: 1px solid #e1e4e8; }}
        th {{ background: #f6f8fa; font-weight: 600; }}
        code {{ font-family: 'SFMono-Regular', Consolas, monospace; }}
        .footer {{ margin-top: 40px; color: #6a737d; font-size: 0.85em; }}
        a {{ color: #0366d6; }}
    </style>
</head>
<body>
    <h1>MNOS16 Unit Test Coverage</h1>
    <div class="overall">
        <div class="metrics">
            <div class="metric">
                <strong>Statement:</strong> <code>{overall_bar}</code> <strong>{overall_pct:.0f}%</strong>
                ({hit_addrs}/{total_addrs} addresses)
            </div>
            <div class="metric">
                <strong>Branch:</strong> <code>{branch_bar}</code> <strong>{overall_branch_pct:.0f}%</strong>
                ({covered_outcomes}/{total_outcomes} outcomes)
            </div>
        </div>
    </div>
    <table>
        <tr><th>Routine</th><th>Total Addrs</th><th>Hit</th><th>Statement</th><th>Branch</th></tr>
{rows}
    </table>
    <p class="footer">
        Generated {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')}
        · <a href="trend.html">Coverage Trend →</a>
        · Tier 1 only (pure-logic unit tests via Unicorn Engine)
        · See <a href="https://github.com/AmbaneP/mini-os/blob/main/doc/TESTING.md">TESTING.md</a>
    </p>
</body>
</html>"""
    (out / "index.html").write_text(html, encoding="utf-8")


def _generate_trend_html(out: Path):
    """Generate a Chart.js trend page from history.json."""
    history_path = out / "history.json"
    if not history_path.exists():
        return

    html = """<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <title>MNOS16 Coverage Trend</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
               max-width: 900px; margin: 40px auto; padding: 0 20px;
               color: #24292e; background: #fff; }
        h1 { border-bottom: 1px solid #e1e4e8; padding-bottom: 8px; }
        .chart-container { position: relative; height: 400px; margin: 20px 0; }
        .footer { margin-top: 40px; color: #6a737d; font-size: 0.85em; }
        a { color: #0366d6; }
    </style>
</head>
<body>
    <h1>Coverage Trend</h1>
    <div class="chart-container">
        <canvas id="trendChart"></canvas>
    </div>
    <p class="footer">
        <a href="index.html">← Current Coverage Report</a>
        · Shows last 50 CI runs
    </p>
    <script>
    fetch('history.json')
        .then(r => r.json())
        .then(history => {
            const labels = history.map(h => {
                const d = new Date(h.timestamp);
                return d.toLocaleDateString('en-US', {month:'short', day:'numeric'});
            });
            const stmtData = history.map(h => h.statement_coverage);
            const branchData = history.map(h => h.branch_coverage);

            new Chart(document.getElementById('trendChart'), {
                type: 'line',
                data: {
                    labels: labels,
                    datasets: [
                        {
                            label: 'Statement Coverage %',
                            data: stmtData,
                            borderColor: '#2188ff',
                            backgroundColor: 'rgba(33, 136, 255, 0.1)',
                            fill: true,
                            tension: 0.3,
                        },
                        {
                            label: 'Branch Coverage %',
                            data: branchData,
                            borderColor: '#28a745',
                            backgroundColor: 'rgba(40, 167, 69, 0.1)',
                            fill: true,
                            tension: 0.3,
                        }
                    ]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    scales: {
                        y: {
                            min: 0,
                            max: 100,
                            title: { display: true, text: 'Coverage %' }
                        }
                    },
                    plugins: {
                        title: {
                            display: true,
                            text: 'MNOS16 Test Coverage Over Time'
                        }
                    }
                }
            });
        });
    </script>
</body>
</html>"""
    (out / "trend.html").write_text(html, encoding="utf-8")


def print_summary(summary: dict):
    """Print a markdown-formatted summary to stdout (for GitHub Actions Job Summary)."""
    print("## Unit Test Coverage\n")
    print(f"**Statement: {summary['overall_coverage']:.0f}%** "
          f"({summary['hit_addresses']}/{summary['total_addresses']} addresses) "
          f"| **Branch: {summary['branch_coverage']:.0f}%** "
          f"({summary['branch_outcomes_covered']}/{summary['branch_outcomes_total']} outcomes)\n")
    print("| Routine | Statement | Branch |")
    print("|---------|-----------|--------|")
    for name, r in sorted(summary["routines"].items()):
        pct = r["coverage"]
        filled = int(pct / 10)
        bar = "#" * filled + "-" * (10 - filled)
        br_pct = r.get("branch_coverage")
        br_str = f"{br_pct:.0f}%" if br_pct is not None else "—"
        print(f"| `{name}` | {bar} {pct:.0f}% | {br_str} |")
    print()


if __name__ == "__main__":
    # When run standalone, read summary.json and print markdown
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="coverage")
    parser.add_argument("--min-coverage", type=float, default=0)
    args = parser.parse_args()

    summary_path = Path(args.output) / "summary.json"
    if summary_path.exists():
        summary = json.loads(summary_path.read_text())
        print_summary(summary)
        if args.min_coverage > 0 and summary["overall_coverage"] < args.min_coverage:
            print(f"\n❌ Coverage {summary['overall_coverage']:.0f}% "
                  f"is below minimum {args.min_coverage:.0f}%")
            sys.exit(1)
    else:
        print("No summary.json found. Run pytest first.")
        sys.exit(1)
