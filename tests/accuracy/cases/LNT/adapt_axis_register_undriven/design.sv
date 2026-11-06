/*
Copyright (c) 2014-2018 Alex Forencich
// Source path: rtl/axis_register.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce

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
// Adapted from rtl/axis_register.v.
// Reduced and modified for the SVerity golden set.
module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       valid_i,
  input  logic       ready_i,
  input  logic [7:0] data_i,
  output logic       valid_o,
  output logic       ready_o,
  output logic [7:0] data_o
);
  logic [7:0] data_q;
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      valid_o <= 1'b0;
      data_q <= 8'h00;
    end else if (ready_i) begin
      valid_o <= valid_i;
      if (valid_i)
        data_q <= data_i;
    end
  end
  assign data_o = data_q;
endmodule
