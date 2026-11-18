// Adapted from verilog-axis rtl/axis_stat_counter.v (48ff7a7e2ef782cf778d47910cf85835c64b1bce).
// Copyright (c) 2014-2018 Alex Forencich. MIT License.
module top #(
  parameter int KEEP_WIDTH = 8,
  parameter int LEN_WIDTH  = 16,
  localparam int CNT_W     = $clog2(KEEP_WIDTH + 1)
) (
  input  logic                  clk_i,
  input  logic                  rst_ni,
  input  logic [KEEP_WIDTH-1:0] tkeep_i,
  input  logic                  tvalid_i,
  input  logic                  tready_i,
  input  logic                  tlast_i,
  output logic [LEN_WIDTH-1:0]  frame_len_o,
  output logic                  frame_len_valid_o
);
  logic [LEN_WIDTH-1:0] len_q, len_d;
  logic                 valid_q, valid_d;
  logic [CNT_W-1:0]     bytes;

  always_comb begin
    len_d   = len_q;
    valid_d = 1'b0;
    bytes   = '0;

    if (valid_q) len_d = '0;

    if (tvalid_i && tready_i) begin
      if (tlast_i) valid_d = 1'b1;
      for (int i = 0; i <= KEEP_WIDTH; i++) begin
        if (tkeep_i[i]) bytes = bytes + 1'b1;
      end
      len_d = len_d + LEN_WIDTH'(bytes);
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      len_q   <= '0;
      valid_q <= 1'b0;
    end else begin
      len_q   <= len_d;
      valid_q <= valid_d;
    end
  end

  assign frame_len_o       = len_q;
  assign frame_len_valid_o = valid_q;
endmodule
