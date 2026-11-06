// Adapted from alexforencich/verilog-axis rtl/axis_frame_len.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
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
  parameter int unsigned KEEP_WIDTH = 4,
  parameter int unsigned LEN_WIDTH = 12
) (
  input  logic                   clk,
  input  logic                   rst,
  input  logic [KEEP_WIDTH-1:0]  monitor_tkeep,
  input  logic                   monitor_tvalid,
  input  logic                   monitor_tready,
  input  logic                   monitor_tlast,
  output logic [LEN_WIDTH-1:0]   frame_len,
  output logic                   frame_len_valid
);
  logic [LEN_WIDTH-1:0] frame_len_next;
  logic                 frame_len_valid_next;
  int unsigned          byte_count;

  always_comb begin
    frame_len_next = frame_len;
    frame_len_valid_next = 1'b0;
    byte_count = 0;
    if (frame_len_valid) frame_len_next = '0;
    if (monitor_tvalid && monitor_tready) begin
      for (int i = 0; i < KEEP_WIDTH; i++) byte_count += monitor_tkeep[i];
      frame_len_next = frame_len_next + LEN_WIDTH'(byte_count);
      frame_len_valid_next = monitor_tlast;
    end
  end

  always_ff @(posedge clk) begin
    if (rst) begin
      frame_len <= '0;
      frame_len_valid <= 1'b0;
    end else begin
      frame_len <= frame_len_next;
      frame_len_valid <= frame_len_valid_next;
    end
  end
endmodule
