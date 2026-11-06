// Adapted from alexforencich/verilog-axis rtl/axis_srl_fifo.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
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

module top #(
  parameter int unsigned DEPTH = 4,
  parameter int unsigned DATA_WIDTH = 8,
  localparam int unsigned PTR_WIDTH = $clog2(DEPTH + 1)
) (
  input logic clk, input logic rst,
  input logic [DATA_WIDTH-1:0] s_data,
  input logic s_valid, output logic s_ready,
  input logic m_ready,
  output logic [DATA_WIDTH-1:0] m_data,
  output logic m_valid,
  output logic [PTR_WIDTH-1:0] count
);
  logic [DATA_WIDTH-1:0] data_reg [DEPTH];
  logic shift, inc, dec;

  assign s_ready = count < DEPTH;
  assign m_valid = count != 0;

  always_comb begin
    m_data = '0;
    if (m_valid) m_data = data_reg[count-1'b1];
  end

  always_comb begin
    shift = 1'b0;
    inc = 1'b0;
    dec = 1'b0;
    if (m_ready && s_valid && s_ready) begin
      shift = 1'b1;
      inc = count == 0;
    end else if (m_ready && m_valid) begin
      dec = 1'b1;
    end else if (s_valid && s_ready) begin
      shift = 1'b1;
      inc = 1'b1;
    end
  end

  always_ff @(posedge clk) begin
    if (rst) begin
      count <= '0;
    end else begin
      if (inc) count <= count + 1'b1;
      else if (dec) count <= count - 1'b1;
      if (shift) begin
        data_reg[0] <= s_data;
        for (int i = 0; i < DEPTH-1; i++) data_reg[i+1] <= data_reg[i];
      end
    end
  end
endmodule
