/*
 * Adapted from verilog-axis rtl/axis_async_fifo_adapter.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
 * Copyright (c) 2019 Alex Forencich
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 * Reduced and modified for the SVerity golden set.
 */

(* black_box *)
module axis_async_fifo_adapter_stub (
  input logic s_clk, input logic s_rst, input logic [7:0] s_data,
  input logic s_valid, output logic s_ready,
  input logic m_clk, input logic m_rst, output logic [7:0] m_data,
  output logic m_valid, input logic m_ready
);
endmodule

module top (
  input logic s_clk, input logic s_rst, input logic [7:0] s_data,
  input logic s_valid, output logic s_ready,
  input logic m_clk, input logic m_rst, output logic [7:0] m_data,
  output logic m_valid, input logic m_ready
);
  axis_async_fifo_adapter_stub u_fifo (
    .s_clk, .s_rst, .s_data, .s_valid, .s_ready,
    .m_clk, .m_rst, .m_data, .m_valid, .m_ready
  );
endmodule
