// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Adapted from hw/ip/prim/rtl/prim_fifo_async.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk_a,
  input  logic       clk_b,
  input  logic       rst_a_ni,
  input  logic       rst_b_ni,
  input  logic       write_a,
  input  logic       read_b,
  output logic       full_b
);
  logic [2:0] wptr_bin_a;
  logic [2:0] rptr_bin_b;
  logic [2:0] wptr_bin_b;

  always_ff @(posedge clk_a or negedge rst_a_ni) begin
    if (!rst_a_ni) begin
      wptr_bin_a <= 3'b000;
    end else if (write_a) begin
      wptr_bin_a <= wptr_bin_a + 3'b001;
    end
  end

  always_ff @(posedge clk_b or negedge rst_b_ni) begin
    if (!rst_b_ni) begin
      rptr_bin_b <= 3'b000;
      wptr_bin_b <= 3'b000;
    end else begin
      wptr_bin_b <= wptr_bin_a;
      if (read_b)
        rptr_bin_b <= rptr_bin_b + 3'b001;
    end
  end

  assign full_b = (wptr_bin_b == {~rptr_bin_b[2:1], rptr_bin_b[0]});
endmodule
