// sverity-args: --top top
// expect: LNT-045 WARNING
// expect-unresolved: LNT-045
module top(input logic [3:0] a, input logic [1:0] i, output logic y);
  always @(a[i]) y = a[3];
endmodule
