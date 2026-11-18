// Heartbeat generator in the PLL output domain, with a lock indicator
// synchronised back to the reference clock. pll_wrap comes from the platform library.
module top (
  input  logic       clk_ref,
  input  logic       rst_n,
  output logic       locked_o,
  output logic [3:0] beat
);
  logic pll_clk;
  logic pll_lock;
  logic lock_s1_q;
  logic lock_s2_q;
  logic [3:0] beat_q;

  pll_wrap u_pll (
    .refclk (clk_ref),
    .rst_n  (rst_n),
    .outclk (pll_clk),
    .locked (pll_lock)
  );

  always_ff @(posedge clk_ref or negedge rst_n) begin
    if (!rst_n) begin
      lock_s1_q <= 1'b0;
      lock_s2_q <= 1'b0;
    end else begin
      lock_s1_q <= pll_lock;
      lock_s2_q <= lock_s1_q;
    end
  end

  always_ff @(posedge pll_clk or negedge rst_n) begin
    if (!rst_n) beat_q <= 4'h0;
    else        beat_q <= beat_q + 4'h1;
  end

  assign locked_o = lock_s2_q;
  assign beat     = beat_q;
endmodule
