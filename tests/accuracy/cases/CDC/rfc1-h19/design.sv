// Input sampled in two clock domains.
module top (
  input  logic src_clk,
  input  logic dst_clk,
  input  logic d_i,
  output logic src_o,
  output logic dst_o
);
  logic src_q;
  logic dst_q;

  always_ff @(posedge src_clk) src_q <= d_i;
  always_ff @(posedge dst_clk) dst_q <= d_i;

  assign src_o = src_q;
  assign dst_o = dst_q;
endmodule
