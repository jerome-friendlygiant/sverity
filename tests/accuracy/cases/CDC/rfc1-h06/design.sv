// Status bit crossing with half-cycle second stage.
module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic d_i,
  output logic q_o
);
  logic src_q;
  logic s1_q;
  logic s2_q;
  logic out_q;

  always_ff @(posedge clk_a) src_q <= d_i;
  always_ff @(posedge clk_b) s1_q <= src_q;
  always_ff @(negedge clk_b) s2_q <= s1_q;
  always_ff @(posedge clk_b) out_q <= s2_q;

  assign q_o = out_q;
endmodule
