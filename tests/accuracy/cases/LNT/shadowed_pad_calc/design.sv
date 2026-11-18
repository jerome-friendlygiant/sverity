// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic [1:0] size,
  input  logic [7:0] bytes_left_i,
  input  logic       beat_i,
  output logic [3:0] pad_o,
  output logic       last_o
);

  function automatic logic [3:0] pad_bytes(input logic [7:0] left, input logic [1:0] size);
    logic [3:0] width_bytes;
    logic [3:0] rem;
    width_bytes = 4'd1 << size;
    rem         = left[3:0] & (width_bytes - 4'd1);
    return (rem == 4'd0) ? 4'd0 : (width_bytes - rem);
  endfunction

  logic [3:0] pad_q;
  logic       last_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      pad_q  <= '0;
      last_q <= 1'b0;
    end else if (beat_i) begin
      pad_q  <= pad_bytes(bytes_left_i, size);
      last_q <= (bytes_left_i <= 8'd16) && (size < 2'd3);
    end
  end

  assign pad_o  = pad_q;
  assign last_o = last_q;

endmodule
