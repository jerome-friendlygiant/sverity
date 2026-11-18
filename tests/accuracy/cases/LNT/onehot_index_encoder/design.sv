// Adapted from common_cells src/cc_onehot_to_bin.sv (e73baaec2ca665cd80c3c384e9258e35242b829c).
// Copyright 2018 ETH Zurich and University of Bologna. Solderpad Hardware License, Version 0.51. SPDX-License-Identifier: SHL-0.51
module top #(
  parameter int unsigned OnehotWidth = 16,
  localparam int unsigned BinWidth   = (OnehotWidth == 1) ? 1 : $clog2(OnehotWidth)
) (
  input  logic                   clk_i,
  input  logic                   rst_ni,
  input  logic [OnehotWidth-1:0] grant_i,
  output logic [BinWidth-1:0]    index_o,
  output logic                   any_o,
  output logic                   multi_o
);
  logic [BinWidth-1:0] index_d;
  logic [BinWidth-1:0] index_q;

  for (genvar j = 0; j < BinWidth; j++) begin : gen_jl
    logic [OnehotWidth-1:0] tmp_mask;
    for (genvar i = 0; i < OnehotWidth; i++) begin : gen_il
      logic [BinWidth-1:0] tmp_i;
      assign tmp_i = BinWidth'(i);
      assign tmp_mask[i] = tmp_i[j];
    end
    assign index_d[j] = |(tmp_mask & grant_i);
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)        index_q <= '0;
    else if (any_o)     index_q <= index_d;
  end

  assign index_o = index_q;
  assign any_o   = |grant_i;
  assign multi_o = (grant_i & (grant_i - 1'b1)) != '0;
endmodule
