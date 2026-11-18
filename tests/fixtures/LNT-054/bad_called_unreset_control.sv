// expect: LNT-054 WARNING 4
module top(input logic clk, d, output logic q);
  logic state; task update; state <= d; endtask
  always @(posedge clk) update(); always_comb if (state) q = d; else q = 0;
endmodule
