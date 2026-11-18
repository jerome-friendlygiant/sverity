// sverity-args: --top top
// expect: LNT-022 WARNING
// expect-confidence: LNT-022 established
module top(input logic clk, d, output logic q);
  wire alias_clk = clk;
  always @(posedge clk) q <= d;
  always @(negedge alias_clk) q <= d;
endmodule
