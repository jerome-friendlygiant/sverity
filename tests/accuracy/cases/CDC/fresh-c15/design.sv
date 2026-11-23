module top(input logic clk_a, clk_b, input logic valid_a, input logic [3:0] payload_a, output logic [3:0] payload_b);
  always_ff @(posedge clk_b) if (valid_a) payload_b <= payload_a;
endmodule
