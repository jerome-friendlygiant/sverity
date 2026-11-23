import io
import json
import pathlib
import subprocess
import sys
import tempfile
from contextlib import redirect_stderr, redirect_stdout
from types import SimpleNamespace
from unittest.mock import patch


def check_wrapper_stdout(binary, root):
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools/dev"))
    import clock_inventory

    def checkout(scratch, *, stdout):
        print("checkout log", file=stdout)
        return scratch

    def setup(scratch, repo, *, stdout):
        print("FuseSoC setup log", file=stdout)
        return scratch, repo / "design.yml"

    def run(command, **_):
        if command[0] != "bash":
            return subprocess.CompletedProcess(command, 0, "one source", "")
        return subprocess.CompletedProcess(command, 0, '{"schema":1}', "")

    stdout, stderr = io.StringIO(), io.StringIO()
    args = ["clock_inventory.py", str(binary), str(root), "--sdc", str(root / "clocks.sdc")]
    with (patch.object(sys, "argv", args),
          patch.object(clock_inventory.capacity, "checkout", checkout),
          patch.object(clock_inventory.capacity, "setup_fusesoc", setup),
          patch.object(clock_inventory.capacity, "available_gb", return_value=64),
          patch.object(clock_inventory.shutil, "disk_usage", return_value=SimpleNamespace(free=2**38)),
          patch.object(clock_inventory.subprocess, "run", side_effect=run),
          redirect_stdout(stdout), redirect_stderr(stderr)):
        assert clock_inventory.main() == 0
    assert json.loads(stdout.getvalue()) == {"schema": 1}, stdout.getvalue()
    assert "checkout log\nFuseSoC setup log\n" in stderr.getvalue(), stderr.getvalue()


def main():
    binary = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temp:
        root = pathlib.Path(temp)
        (root / "top.sv").write_text("""
module top(input logic clk, input logic data, output logic q);
  always_ff @(posedge clk) q <= data;
endmodule
""")
        (root / "design.f").write_text(str(root / "top.sv") + "\n")
        (root / "clocks.sdc").write_text("""
create_clock -name primary -period 10 [get_ports clk]
create_clock -name competing -period 20 [get_ports clk]
create_clock -name unused -period 30 [get_ports missing_clock]
create_clock -name unused -period 40 [get_ports clk]
""")
        command = [str(binary), "--top", "top", "-f", str(root / "design.f"),
                   "--sdc", str(root / "clocks.sdc")]
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        data = json.loads(result.stdout)
        assert len(data["roots"]) == 1, data
        assert data["roots"][0]["ambiguous"] is True, data
        assert data["roots"][0]["sdc_clock"] in {"primary", "competing"}, data
        declarations = {row["name"]: row for row in data["declarations"]}
        assert declarations["primary"]["mapped_domains"] == [0], data
        assert declarations["competing"]["mapped_domains"] == [0], data
        unused = [row for row in data["declarations"] if row["name"] == "unused"]
        assert [row["active"] for row in unused] == [False, True], data
        assert [row["unmapped"] for row in unused] == [False, False], data

        (root / "clocks.sdc").write_text("""create_clock -name bare -period 10 clk
create_clock -name cell -period 20 [get_cells clk]
""")
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        data = json.loads(result.stdout)
        assert data["roots"][0]["sdc_clock"] == "", data
        assert len(data["declarations"]) == 2, data
        assert all(row["unmapped"] and row["mapped_domains"] == [] for row in data["declarations"]), data

        (root / "top.sv").write_text("""module top(input logic clk, output logic q);
  always_ff @(posedge clk) q <= missing;
endmodule
""")
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode != 0 and not result.stdout, result
        assert "model elaboration failed" in result.stderr and "missing" in result.stderr, result
        check_wrapper_stdout(binary, root)


if __name__ == "__main__":
    main()
