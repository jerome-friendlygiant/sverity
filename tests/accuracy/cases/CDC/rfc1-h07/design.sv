// Parameter word handoff.
module top (
  input  logic       src_clk,
  input  logic       dst_clk,
  input  logic [7:0] data_i,
  output logic [7:0] data_o
);
  logic [7:0] tx_q;
  logic [7:0] rx_q;

  always_ff @(posedge src_clk) tx_q <= data_i;
  always_ff @(posedge dst_clk) rx_q <= tx_q;

  assign data_o = rx_q;
endmodule
