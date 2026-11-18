// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top (
  input  logic               clk_i,
  input  logic               rst_ni,
  input  logic               sample_i,
  input  logic signed [11:0] ts_local_i,
  input  logic signed [11:0] ts_remote_i,
  input  logic        [11:0] max_skew_i,
  output logic               remote_late_o,
  output logic        [11:0] abs_skew_o,
  output logic               skew_valid_o
);
  logic signed [11:0] skew_d;
  logic signed [11:0] skew_q;
  logic               valid_q;

  assign skew_d = ts_remote_i - ts_local_i;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      skew_q  <= '0;
      valid_q <= 1'b0;
    end else begin
      valid_q <= sample_i;
      if (sample_i) skew_q <= skew_d;
    end
  end

  assign remote_late_o = valid_q & (skew_q < max_skew_i);
  assign abs_skew_o    = skew_q[11] ? -skew_q : skew_q;
  assign skew_valid_o  = valid_q;
endmodule
