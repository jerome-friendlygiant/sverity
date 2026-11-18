// sverity-args: --top top
// expect: LNT-079 WARNING
module top(input logic clk, input logic d, output logic q);
  always @(posedge clk) force q = d;
endmodule
// revert: force q = d; => q = d;
// revert-expect-none: LNT-079
