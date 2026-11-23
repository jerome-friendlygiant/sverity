// Status bit sampled on both clock edges.
module top (
  input  logic src_clk,
  input  logic dst_clk,
  input  logic d_i,
  output logic q_o
);
  logic src_q;
  logic s1_q;
  logic s2_q;
  logic out_q;

  always_ff @(posedge src_clk) src_q <= d_i;

  always @(posedge dst_clk or negedge dst_clk) s1_q <= src_q;
  always_ff @(posedge dst_clk) s2_q <= s1_q;
  always_ff @(posedge dst_clk) out_q <= s2_q;

  assign q_o = out_q;
endmodule
