// Selects between a cold and a warm reset for a block that is power-gated.
// Library cell: body not supplied.
(* black_box *) module rstmux_cell (input logic A, input logic B, input logic S, output logic Y);
endmodule

module top (
  input  logic clk,
  input  logic por_ni,
  input  logic warm_rst_ni,
  input  logic warm_i,
  output logic blk_q_o
);
  logic [1:0] cold_sync_q;
  logic [1:0] warm_sync_q;
  logic       sel_q;
  logic       blk_rst_n;
  logic       blk_q;

  always_ff @(posedge clk or negedge por_ni)
    if (!por_ni) cold_sync_q <= 2'b00;
    else         cold_sync_q <= {cold_sync_q[0], 1'b1};

  always_ff @(posedge clk or negedge warm_rst_ni)
    if (!warm_rst_ni) warm_sync_q <= 2'b00;
    else              warm_sync_q <= {warm_sync_q[0], 1'b1};

  always_ff @(posedge clk or negedge por_ni)
    if (!por_ni) sel_q <= 1'b0;
    else         sel_q <= warm_i;

  rstmux_cell u_rmux (.A(cold_sync_q[1]), .B(warm_sync_q[1]), .S(sel_q), .Y(blk_rst_n));

  always_ff @(posedge clk or negedge blk_rst_n)
    if (!blk_rst_n) blk_q <= 1'b0;
    else            blk_q <= 1'b1;

  assign blk_q_o = blk_q;
endmodule
