// Adapted from alexforencich/verilog-axis rtl/axis_fifo.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
// Copyright (c) 2013-2023 Alex Forencich. Permission is hereby granted under the MIT License.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk,
  input  logic       rst,
  input  logic [15:0] s_axis_tdata,
  input  logic       s_axis_tvalid,
  output logic       s_axis_tready,
  output logic [15:0] m_axis_tdata,
  output logic       m_axis_tvalid,
  input  logic       m_axis_tready
);
  logic [15:0] mem_q [0:3];
  logic [1:0] wr_ptr_q, rd_ptr_q;
  logic [2:0] count_q;
  logic do_write, do_read;

  assign s_axis_tready = (count_q != 3'd4);
  assign m_axis_tvalid = (count_q != 3'd0);
  assign m_axis_tdata  = mem_q[rd_ptr_q];
  assign do_write = s_axis_tvalid & s_axis_tready;
  assign do_read  = m_axis_tvalid & m_axis_tready;

  always_ff @(posedge clk) begin
    if (rst) begin
      wr_ptr_q <= '0;
      rd_ptr_q <= '0;
      count_q  <= '0;
    end else begin
      if (do_write) begin
        mem_q[wr_ptr_q] <= s_axis_tdata;
        wr_ptr_q <= wr_ptr_q + 2'd1;
      end
      if (do_read) rd_ptr_q <= rd_ptr_q + 2'd1;
      case ({do_write, do_read})
        2'b10: count_q <= count_q + 3'd1;
        2'b01: count_q <= count_q - 3'd1;
        default: ;
      endcase
    end
  end
endmodule
