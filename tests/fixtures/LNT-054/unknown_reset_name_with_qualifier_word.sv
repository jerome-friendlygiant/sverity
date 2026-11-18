// sverity-args: --top top
// A word before the reset word (`force_rst`, `en_rst_n`) or one after it that may name a reset pulse or a request (`rst_pulse`,
// `rst_gen_n`, `rst_start`) leaves the identity undecided: none of the registers is called reset or unreset.
// expect-count: LNT-054 WARNING 5
// expect-unresolved: LNT-054 17
// expect-unresolved: LNT-054 18
// expect-unresolved: LNT-054 19
// expect-unresolved: LNT-054 20
// expect-unresolved: LNT-054 21
module top(input logic clk, force_rst, en_rst_n, rst_pulse, rst_gen_n, rst_start, d, output logic [4:0] y);
  logic a, b, c, e, f;
  always_ff @(posedge clk) if (force_rst) a <= 1'b0; else a <= d;
  always_ff @(posedge clk) if (!en_rst_n) b <= 1'b0; else b <= d;
  always_ff @(posedge clk) if (rst_pulse) c <= 1'b0; else c <= d;
  always_ff @(posedge clk) if (!rst_gen_n) e <= 1'b0; else e <= d;
  always_ff @(posedge clk) if (rst_start) f <= 1'b0; else f <= d;
  always_comb if (a) y[0] = d; else y[0] = 1'b0;
  always_comb if (b) y[1] = d; else y[1] = 1'b0;
  always_comb if (c) y[2] = d; else y[2] = 1'b0;
  always_comb if (e) y[3] = d; else y[3] = 1'b0;
  always_comb if (f) y[4] = d; else y[4] = 1'b0;
endmodule
