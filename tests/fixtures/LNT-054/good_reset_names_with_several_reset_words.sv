// sverity-args: --top top
// A name with several reset words is a reset when one of them is followed only by polarity, sync, domain or another reset word
// (`por_rst_n`, `rst_por_n`, `reset_reset_n`); so are the async-FIFO domain letters (`wrst_n`, `rrst_n`) and `ndmreset`.
// expect-none: LNT-054
module top(input logic clk, por_rst_n, rst_por_n, sys_por_rst_n, reset_reset_n, rst_lc_n, wrst_n, rrst_n, ndmreset, d, output logic y);
  logic a, b, c, e, f, g, h, i;
  always_ff @(posedge clk) if (!por_rst_n) a <= 1'b0; else a <= d;
  always_ff @(posedge clk) if (!rst_por_n) b <= 1'b0; else b <= d;
  always_ff @(posedge clk) if (!sys_por_rst_n) c <= 1'b0; else c <= d;
  always_ff @(posedge clk) if (!reset_reset_n) e <= 1'b0; else e <= d;
  always_ff @(posedge clk) if (!rst_lc_n) f <= 1'b0; else f <= d;
  always_ff @(posedge clk) if (!wrst_n) g <= 1'b0; else g <= d;
  always_ff @(posedge clk) if (!rrst_n) h <= 1'b0; else h <= d;
  always_ff @(posedge clk) if (ndmreset) i <= 1'b0; else i <= d;
  always_comb begin
    if (a & b & c & e & f & g & h & i) y = d; else y = 1'b0;
  end
endmodule
