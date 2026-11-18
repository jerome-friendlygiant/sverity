// sverity-args: --top top
// `rst || clear` holds whenever `rst` is asserted, so its `then` branch is a reset branch.
// expect-none: LNT-054
module top(input logic clk, rst, clear, d, output logic y);
  logic q;
  always_ff @(posedge clk) if (rst || clear) q <= 1'b0; else q <= d;
  always_comb begin
    if (q) y = d; else y = 1'b0;
  end
endmodule
