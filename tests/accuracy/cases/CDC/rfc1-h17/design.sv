// Locally gated capture block.
module top (
  input  logic clk_b,
  input  logic en_i,
  input  logic d_i,
  output logic q_o
);
  logic en_b_q;
  logic gclk_b;
  logic q_q;

  always_ff @(posedge clk_b) en_b_q <= en_i;

  assign gclk_b = clk_b & en_b_q;

  always_ff @(posedge gclk_b) q_q <= d_i;

  assign q_o = q_q;
endmodule
