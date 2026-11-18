// Adapted from common_cells src/cc_lfsr_8bit.sv
// (commit e73baaec2ca665cd80c3c384e9258e35242b829c).
// Copyright 2018 ETH Zurich and University of Bologna.
// Licensed under the Solderpad Hardware License, Version 0.51 (SHL-0.51).

module top #(
  parameter logic        [7:0] Seed  = 8'b1,
  parameter int unsigned       Width = 8
) (
  input  logic                     clk_i,
  input  logic                     rst_ni,
  input  logic                     clr_i,
  input  logic                     en_i,
  output logic [        Width-1:0] refill_way_oh_o,
  output logic [$clog2(Width)-1:0] refill_way_bin_o
);

  localparam int unsigned LogWidth = $clog2(Width);

  logic [7:0] shift_d, shift_q;

  always_comb begin
    automatic logic shift_in;
    shift_in = !(shift_q[7] ^ shift_q[3] ^ shift_q[2] ^ shift_q[1]);

    shift_d = shift_q;

    if (en_i) shift_d = {shift_q[6:0], shift_in};

    refill_way_oh_o = '0;
    refill_way_oh_o[shift_q[LogWidth-1:0]] = 1'b1;
    refill_way_bin_o = shift_q[LogWidth-1:0];
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)    shift_q <= Seed;
    else if (clr_i) shift_q <= Seed;
    else            shift_q <= shift_d;
  end

endmodule
