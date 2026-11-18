// sverity-args: --top top
// A reset word with only polarity, sync or domain words beside it is a reset with no doubt (`rst_core_n`, `reset_ddr`, `PORESETn`).
// expect-none: LNT-054
module top(input logic clk, rst_core_n, rst_aon_ni, reset_ddr, rst_n_meta, PRESET_N, PORESETn, DBGRESETn, nTRST, sw_rst, d, output logic y);
  logic a, b, c, e, f, g, h, i, j;
  always_ff @(posedge clk) if (!rst_core_n) a <= 1'b0; else a <= d;
  always_ff @(posedge clk) if (!rst_aon_ni) b <= 1'b0; else b <= d;
  always_ff @(posedge clk) if (reset_ddr) c <= 1'b0; else c <= d;
  always_ff @(posedge clk) if (!rst_n_meta) e <= 1'b0; else e <= d;
  always_ff @(posedge clk) if (!PRESET_N) f <= 1'b0; else f <= d;
  always_ff @(posedge clk) if (!PORESETn) g <= 1'b0; else g <= d;
  always_ff @(posedge clk) if (!DBGRESETn) h <= 1'b0; else h <= d;
  always_ff @(posedge clk) if (!nTRST) i <= 1'b0; else i <= d;
  always_ff @(posedge clk) if (sw_rst) j <= 1'b0; else j <= d;
  always_comb begin
    if (a & b & c & e & f & g & h & i & j) y = d; else y = 1'b0;
  end
endmodule
