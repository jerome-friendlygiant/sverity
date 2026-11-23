// Status port crossing the chip boundary.
module top (
  input  logic src_clk,
  input  logic dst_clk,
  input  logic d_i,
  input  logic e_i,
  output logic stat_o,
  output logic aux_o
);
  logic stat_q;
  logic aux_q;

  always_ff @(posedge src_clk) stat_q <= d_i;
  always_ff @(posedge dst_clk) aux_q <= e_i;

  assign stat_o = stat_q;
  assign aux_o  = aux_q;
endmodule
