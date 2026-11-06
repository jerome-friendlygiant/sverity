// Adapted from alexforencich/verilog-axis rtl/axis_broadcast.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
/*

Copyright (c) 2019 Alex Forencich

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
  parameter int unsigned M_COUNT = 4,
  parameter int unsigned DATA_WIDTH = 8
) (
  input  logic [DATA_WIDTH-1:0] s_data,
  input  logic                  s_valid,
  output logic                  s_ready,
  output logic [M_COUNT*DATA_WIDTH-1:0] m_data,
  output logic [M_COUNT-1:0]    m_valid,
  input  logic [M_COUNT-1:0]    m_ready
);
  // A broadcast transfer completes only when every output accepts it.
  assign s_ready = &m_ready;
  assign m_data = {M_COUNT{s_data}};
  assign m_valid = {M_COUNT{s_valid}};
endmodule
