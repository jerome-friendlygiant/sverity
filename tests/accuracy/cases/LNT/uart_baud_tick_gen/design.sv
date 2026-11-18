// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top (
  input  logic        clk_i,
  input  logic        rst_ni,
  input  logic        en_i,
  input  logic [15:0] divisor_i,
  output logic        tick_o,
  output logic        sample_tick_o,
  output logic [7:0]  tick_cnt_o
);
  logic [15:0] cnt_q;
  logic [15:0] half_period;
  logic [7:0]  tick_cnt_q;
  logic        tick;

  assign half_period = divisor_i >> 1;
  assign tick        = (cnt_q == divisor_i - 16'd1);

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      cnt_q      <= '0;
      tick_cnt_q <= '0;
    end else begin
      if (!en_i || tick) cnt_q <= '0;
      else               cnt_q <= cnt_q + 16'd1;
      if (en_i && tick)  tick_cnt_q <= tick_cnt_q + 8'd1;
    end
  end

  assign tick_o     = en_i & tick;
  assign sample_tick_o = en_i & (cnt_q == (divisor_i >> 1));
  assign tick_cnt_o = tick_cnt_q;
endmodule
