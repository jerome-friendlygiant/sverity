// sverity-args: --top top
// expect: LNT-022 WARNING
// expect-confidence: LNT-022 established
module top(input logic [1:0] clocks, input logic d, output logic q);
  wire alias_clk = clocks[1];
  always @(posedge clocks[1] or negedge alias_clk) q <= d;
endmodule
