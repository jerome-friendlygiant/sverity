// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top (
  input  logic [11:0] addr_i,
  input  logic [2:0]  size_i,
  input  logic [3:0]  len_i,
  input  logic [1:0]  burst_i,
  output logic [11:0] next_addr_o
);

  localparam logic [1:0] BURST_FIXED = 2'b00;
  localparam logic [1:0] BURST_WRAP  = 2'b10;

  logic [11:0] incr;
  logic [11:0] wrap_bytes;
  logic [11:0] wrap_mask;
  logic [11:0] sum;

  always_comb begin
    case (size_i)
      3'd0:    incr = 12'd1;
      3'd1:    incr = 12'd2;
      3'd2:    incr = 12'd4;
      3'd3:    incr = 12'd8;
      3'd4:    incr = 12'd16;
      3'd5:    incr = 12'd32;
      3'd6:    incr = 12'd64;
      default: incr = 'x;
    endcase
  end

  assign sum        = addr_i + incr;
  assign wrap_bytes = (12'(len_i) + 12'd1) << size_i;
  assign wrap_mask  = wrap_bytes - 12'd1;

  always_comb begin
    if (burst_i == BURST_FIXED) begin
      next_addr_o = addr_i;
    end else if (burst_i == BURST_WRAP) begin
      next_addr_o = (addr_i & ~wrap_mask) | (sum & wrap_mask);
    end else begin
      next_addr_o = sum;
    end
  end

endmodule
