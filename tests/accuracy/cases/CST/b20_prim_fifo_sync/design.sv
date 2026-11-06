// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_fifo_sync.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project). Licensed under the Apache License, Version 2.0. SPDX-License-Identifier: Apache-2.0.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       clr_i,
  input  logic       wvalid_i,
  output logic       wready_o,
  input  logic [7:0] wdata_i,
  output logic       rvalid_o,
  input  logic       rready_i,
  output logic [7:0] rdata_o
);
  logic [7:0] storage_q [0:1];
  logic       wr_ptr_q, rd_ptr_q, full_q, empty_q;
  logic       do_write, do_read;

  assign wready_o = ~full_q;
  assign rvalid_o = ~empty_q;
  assign rdata_o  = storage_q[rd_ptr_q];
  assign do_write = wvalid_i & wready_o;
  assign do_read  = rvalid_o & rready_i;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      wr_ptr_q <= 1'b0;
      rd_ptr_q <= 1'b0;
      full_q   <= 1'b0;
      empty_q  <= 1'b1;
    end else if (clr_i) begin
      wr_ptr_q <= 1'b0;
      rd_ptr_q <= 1'b0;
      full_q   <= 1'b0;
      empty_q  <= 1'b1;
    end else begin
      if (do_write) begin
        storage_q[wr_ptr_q] <= wdata_i;
        wr_ptr_q <= ~wr_ptr_q;
      end
      if (do_read) rd_ptr_q <= ~rd_ptr_q;
      case ({do_write, do_read})
        2'b10: begin empty_q <= 1'b0; if (full_q) full_q <= 1'b1; else if (wr_ptr_q == ~rd_ptr_q) full_q <= 1'b1; end
        2'b01: begin full_q <= 1'b0; if (rd_ptr_q == ~wr_ptr_q) empty_q <= 1'b1; end
        default: ;
      endcase
    end
  end
endmodule
