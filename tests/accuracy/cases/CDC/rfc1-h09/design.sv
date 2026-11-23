// Four-bit control bundle brought into the consumer clock domain.
module top (
  input  logic       src_clk,
  input  logic       dst_clk,
  input  logic [3:0] ctrl_i,
  output logic [3:0] ctrl_o
);
  logic [3:0] ctrl_q;
  logic [3:0] s1_q;
  logic [3:0] s2_q;
  logic [3:2] s3_q;

  always_ff @(posedge src_clk) ctrl_q <= ctrl_i;

  always_ff @(posedge dst_clk) s1_q <= ctrl_q;
  always_ff @(posedge dst_clk) s2_q <= s1_q;
  always_ff @(posedge dst_clk) s3_q <= s2_q[3:2];

  assign ctrl_o = {s3_q, s2_q[1:0]};
endmodule
