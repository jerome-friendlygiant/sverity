// sverity-args: --top top
// expect: LNT-047 WARNING 5
module top(input logic clk, en, a, output logic q);
  logic gclk;
  assign gclk = clk & en;
  always @(posedge gclk) q <= a;
endmodule
// revert: clk & en => clk
// revert-expect-none: LNT-047
