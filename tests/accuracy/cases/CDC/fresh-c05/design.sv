module top(input logic clk_a, clk_b, input logic in_a, output logic out_b);
  always_ff @(posedge clk_b) out_b <= in_a;
endmodule
