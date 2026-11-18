// sverity-args: --top top
// A status word between the reset word and the polarity word (`rst_cnt_n`, `rst_active_n`) is written like a level that asserts
// something, so it may be a reset: undecided, not data. The registers it clears are neither called reset nor unreset.
// expect-count: LNT-054 WARNING 2
// expect-unresolved: LNT-054 11
// expect-unresolved: LNT-054 12
module top(input logic clk, rst_cnt_n, rst_active_n, d, output logic [1:0] y);
  logic a, b;
  always_ff @(posedge clk) if (!rst_cnt_n) a <= 1'b0; else a <= d;
  always_ff @(posedge clk) if (!rst_active_n) b <= 1'b0; else b <= d;
  always_comb if (a) y[0] = d; else y[0] = 1'b0;
  always_comb if (b) y[1] = d; else y[1] = 1'b0;
endmodule
