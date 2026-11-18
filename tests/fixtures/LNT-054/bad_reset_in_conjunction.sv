// sverity-args: --top top
// `rst && en` does not hold whenever `rst` is asserted: `q` is not reset by it.
// expect: LNT-054 WARNING 8
module top(input logic clk, rst, en, d, output logic y);
  logic q;
  always_ff @(posedge clk) if (rst && en) q <= 1'b0; else q <= d;
  always_comb begin
    if (q) y = d; else y = 1'b0;
  end
endmodule
