// Adapted from alexforencich/verilog-axis rtl/priority_encoder.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
/*

Copyright (c) 2014-2021 Alex Forencich

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
  parameter int unsigned WIDTH = 8,
  parameter bit LSB_HIGH_PRIORITY = 0,
  localparam int unsigned ENC_WIDTH = WIDTH > 1 ? $clog2(WIDTH) : 1
) (
  input  logic [WIDTH-1:0] input_unencoded,
  output logic             output_valid,
  output logic [ENC_WIDTH-1:0] output_encoded,
  output logic [WIDTH-1:0] output_unencoded
);
  always_comb begin
    output_valid = 1'b0;
    output_encoded = '0;
    output_unencoded = '0;
    if (LSB_HIGH_PRIORITY) begin
      for (int i = 0; i < WIDTH; i++) begin
        if (input_unencoded[i] && !output_valid) begin
          output_valid = 1'b1;
          output_encoded = ENC_WIDTH'(i);
          output_unencoded[i] = 1'b1;
        end
      end
    end else begin
      for (int i = WIDTH-1; i >= 0; i--) begin
        if (input_unencoded[i] && !output_valid) begin
          output_valid = 1'b1;
          output_encoded = ENC_WIDTH'(i);
          output_unencoded[i] = 1'b1;
        end
      end
    end
  end
endmodule
