// Heartbeat generator: a prescaler toggles a slow clock and an 8-bit
// counter in the slow domain counts heartbeats for the status register.
module top (
  input  logic       clk,
  input  logic       rst_n,
  output logic [7:0] beat_cnt,
  output logic       slow_o
);
  logic [3:0] pre_cnt;
  logic       slow_clk;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      pre_cnt  <= 4'd0;
      slow_clk <= 1'b0;
    end else if (pre_cnt == 4'd7) begin
      pre_cnt  <= 4'd0;
      slow_clk <= ~slow_clk;
    end else begin
      pre_cnt  <= pre_cnt + 4'd1;
    end
  end

  always_ff @(posedge slow_clk or negedge rst_n) begin
    if (!rst_n) beat_cnt <= 8'd0;
    else        beat_cnt <= beat_cnt + 8'd1;
  end

  assign slow_o = slow_clk;
endmodule
