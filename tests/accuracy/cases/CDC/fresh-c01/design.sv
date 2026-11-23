module top(input logic clk_a, clk_b, input logic enable_a, output logic out_b);
  logic gate_en;
  always_ff @(posedge clk_a) gate_en <= enable_a;
  wire clk_g = clk_b & gate_en;
  always_ff @(posedge clk_g) out_b <= ~out_b;
endmodule
