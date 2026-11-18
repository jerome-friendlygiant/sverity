// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top #(
  parameter int unsigned W = 12
) (
  input  logic         clk_i,
  input  logic         rst_ni,
  input  logic         beat_valid_i,
  input  logic         beat_last_i,
  input  logic [3:0]   beat_bytes_i,
  output logic [W-1:0] frame_bytes_o,
  output logic         frame_done_o,
  output logic         frame_busy_o
);

  logic [W-1:0] acc_q;
  logic [W-1:0] len_q;
  logic         done_q;
  logic         busy_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin : acc_seq
    logic [W-1:0] sum;
    if (!rst_ni) begin
      acc_q  <= '0;
      len_q  <= '0;
      done_q <= 1'b0;
      busy_q <= 1'b0;
    end else begin
      sum    = acc_q + W'(beat_bytes_i);
      done_q <= beat_valid_i & beat_last_i;
      if (beat_valid_i) begin
        busy_q <= ~beat_last_i;
        if (beat_last_i) begin
          len_q <= sum;
          acc_q <= '0;
        end else begin
          acc_q <= sum;
        end
      end
    end
  end

  logic [W-1:0] shown;
  always_comb begin
    shown = len_q;
    if (busy_q) shown = acc_q;
  end

  assign frame_bytes_o = shown;
  assign frame_done_o  = done_q;
  assign frame_busy_o  = busy_q;

endmodule
