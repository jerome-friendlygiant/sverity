// sverity-args: --top top
// `presetn`, `rst1` and `sysrst` are resets by their tokens (a token that ends in `reset`/`rst` followed by polarity only).
// expect-none: LNT-054
module top(input logic pclk, presetn, rst1, sysrst, d, output logic y);
  logic a, b, c;
  always_ff @(posedge pclk) if (!presetn) a <= 1'b0; else a <= d;
  always_ff @(posedge pclk) if (rst1) b <= 1'b0; else b <= d;
  always_ff @(posedge pclk) if (sysrst) c <= 1'b0; else c <= d;
  always_comb begin
    if (a & b & c) y = d; else y = 1'b0;
  end
endmodule
