// sverity-args: --top top -f @DIR@/suppress.f
// A -Wno-* hidden inside a filelist must still be recorded as a coverage gap.
// expect-coverage: upstream-diagnostics INCOMPLETE
module top(input logic a, output logic y);
  logic spare;
  assign y = a;
endmodule
