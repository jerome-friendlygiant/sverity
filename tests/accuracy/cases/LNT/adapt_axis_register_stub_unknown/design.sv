// Adapted from alexforencich/verilog-axis rtl/axis_register.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
/*

Copyright (c) 2014-2018 Alex Forencich

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/
// Reduced and modified for the SVerity golden set.

module axis_register_stub #(
  parameter int unsigned DATA_WIDTH = 8,
  parameter int unsigned REG_TYPE = 2
) (
  input  logic clk,
  input  logic rst,
  input  logic [DATA_WIDTH-1:0] s_data,
  input  logic s_valid,
  output logic s_ready,
  input  logic m_ready,
  output logic [DATA_WIDTH-1:0] m_data,
  output logic m_valid
);
endmodule

module top #(
  parameter int unsigned DATA_WIDTH = 8,
  parameter int unsigned REG_TYPE = 2
) (
  input  logic clk,
  input  logic rst,
  input logic [DATA_WIDTH-1:0] s_data,
  input logic s_valid,
  output logic s_ready,
  input logic m_ready,
  output logic [DATA_WIDTH-1:0] m_data,
  output logic m_valid
);
  axis_register_stub #(.DATA_WIDTH(DATA_WIDTH), .REG_TYPE(REG_TYPE)) u_reg (.*);
endmodule
