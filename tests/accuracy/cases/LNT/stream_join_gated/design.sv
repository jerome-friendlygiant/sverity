// Adapted from common_cells src/cc_stream_join_dynamic.sv (e73baaec2ca665cd80c3c384e9258e35242b829c).
// Copyright 2020 ETH Zurich and University of Bologna. Solderpad Hardware License, Version 0.51. SPDX-License-Identifier: SHL-0.51
module top #(
  parameter int unsigned NumInp = 4
) (
  input  logic              clk_i,
  input  logic              rst_ni,
  input  logic [NumInp-1:0] inp_valid_i,
  output logic [NumInp-1:0] inp_ready_o,
  input  logic [NumInp-1:0] sel_i,
  input  logic              start_i,
  output logic              oup_valid_o,
  input  logic              oup_ready_i,
  output logic [7:0]        beat_cnt_o
);
  logic [7:0] beat_q;
  logic       fire;
  logic hold_q;

  assign oup_valid_o = &(inp_valid_i | ~sel_i) && |sel_i && (start_i || hold_q);
  for (genvar i = 0; i < NumInp; i++) begin : gen_inp_ready
    assign inp_ready_o[i] = oup_valid_o & oup_ready_i & sel_i[i];
  end

  assign fire = oup_valid_o & oup_ready_i;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      beat_q   <= '0;
      hold_q   <= 1'b0;
    end else begin
      if (fire) beat_q <= beat_q + 8'd1;
      hold_q   <= oup_valid_o & ~oup_ready_i;
    end
  end

  assign beat_cnt_o = beat_q;
endmodule
