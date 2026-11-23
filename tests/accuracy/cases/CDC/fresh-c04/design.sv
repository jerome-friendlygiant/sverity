module top(input logic clk_a, clk_b, input logic in_a, in_b, output logic out_a, out_b);
  always_ff @(posedge clk_a) out_a <= in_a;
  always_ff @(posedge clk_b) out_b <= in_b;
endmodule
