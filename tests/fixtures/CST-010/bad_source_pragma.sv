// sverity-args: --top top
// An in-source pragma that ignores a diagnostic must be surfaced, never silent.
// expect: CST-010 WARNING
// expect-coverage: upstream-diagnostics INCOMPLETE
module top(input logic a, output logic y);
`pragma diagnostic ignore="-Wunused"
  logic spare;
  assign y = a;
endmodule
