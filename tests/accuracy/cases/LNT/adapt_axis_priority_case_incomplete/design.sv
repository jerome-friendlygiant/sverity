/*
Copyright (c) 2014-2021 Alex Forencich
// Source path: rtl/priority_encoder.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce

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
// Adapted from rtl/priority_encoder.v.
// Reduced and modified for the SVerity golden set.
module top (
  input  logic [3:0] request_i,
  output logic [1:0] index_o,
  output logic       valid_o
);
  always_comb begin
    valid_o = 1'b0;
    casez (request_i)
      4'b1???: begin index_o = 2'd3; valid_o = 1'b1; end
      4'b01??: begin index_o = 2'd2; valid_o = 1'b1; end
      4'b001?: begin index_o = 2'd1; valid_o = 1'b1; end
      4'b0001: begin index_o = 2'd0; valid_o = 1'b1; end
    endcase
  end
endmodule
