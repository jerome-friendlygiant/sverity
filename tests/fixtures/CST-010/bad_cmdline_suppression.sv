// sverity-args: --top top -Wno-unused
// expect-coverage: upstream-diagnostics INCOMPLETE
module top(input logic a, output logic y);
  logic spare;
  assign y = a;
endmodule
