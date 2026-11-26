// sverity-args: --top top
// alias_n is a multiply-driven wand net (rst_n and mask): the guard is not an exact single-driver test of rst_n,
// so the forced value on q is undecided ('?') and RDC-025 is Unresolved (round 4 A2a).
// expect-unresolved: RDC-025
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clk, rst_n, mask, d, output logic q);
  wand alias_n;
  assign alias_n = rst_n;
  assign alias_n = mask;
  always @(posedge clk or negedge rst_n)
    if (!alias_n) q <= 1'b0;
    else q <= d;
endmodule
