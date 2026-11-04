// sverity-args: --top top
// expect-none: LNT-076
module top(input logic clk, input logic a, output logic y, output logic q);
  always_comb y = a;
  always_ff @(posedge clk) q <= a;
endmodule
