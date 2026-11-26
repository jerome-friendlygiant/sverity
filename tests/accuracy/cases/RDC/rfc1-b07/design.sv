// Tracks whether PHY link training has completed.
module top (
  input  logic clk,
  input  logic rst_ni,
  input  logic train_done_i,
  input  logic link_drop_ni,
  output logic trained_o
);
  logic trained_q;

  always_ff @(posedge clk or negedge rst_ni or negedge link_drop_ni)
    if (!link_drop_ni) trained_q <= 1'b0;
    else if (!rst_ni)  trained_q <= 1'b1;
    else               trained_q <= trained_q | train_done_i;

  assign trained_o = trained_q;
endmodule
