// expect: LNT-054 WARNING 5
module top(input logic clk, en, d, output logic q);
  logic state;
  always @(posedge clk) if (en) state <= 0; else state <= d;
  always_comb if (state) q = d; else q = 0;
endmodule
