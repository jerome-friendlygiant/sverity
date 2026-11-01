// sverity-args: --top top
// expect: LNT-055 WARNING 8
// expect: LNT-055 WARNING 9
// expect-count: LNT-055 WARNING 2
module top(input logic clk, output logic neg, neg2);
  logic [7:0] x;
  always_ff @(posedge clk) x <= x - 1;
  assign neg = (x < 0);             // x is unsigned and decremented: never below zero
  assign neg2 = ((x - 1) < 0);      // direct form: unsigned difference below zero
endmodule
