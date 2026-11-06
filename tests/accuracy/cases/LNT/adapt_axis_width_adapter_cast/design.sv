// Adapted from alexforencich/verilog-axis rtl/axis_adapter.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
/*

Copyright (c) 2014-2023 Alex Forencich

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

module top #(
  parameter int unsigned S_DATA_WIDTH = 8,
  parameter int unsigned M_DATA_WIDTH = 16,
  localparam int unsigned S_KEEP_WIDTH = (S_DATA_WIDTH + 7) / 8,
  localparam int unsigned M_KEEP_WIDTH = (M_DATA_WIDTH + 7) / 8
) (
  input  logic [S_DATA_WIDTH-1:0] s_axis_tdata,
  input  logic [S_KEEP_WIDTH-1:0] s_axis_tkeep,
  input  logic                    s_axis_tvalid,
  input  logic                    m_axis_tready,
  output logic                    s_axis_tready,
  output logic [M_DATA_WIDTH-1:0] m_axis_tdata,
  output logic [M_KEEP_WIDTH-1:0] m_axis_tkeep,
  output logic                    m_axis_tvalid
);
  // This is the source adapter's explicit segment extension: the input is
  // placed in the low segment of the wider output bus.
  assign s_axis_tready = m_axis_tready;
  assign m_axis_tdata = M_DATA_WIDTH'(s_axis_tdata);
  assign m_axis_tkeep = M_KEEP_WIDTH'(s_axis_tkeep);
  assign m_axis_tvalid = s_axis_tvalid;
endmodule
