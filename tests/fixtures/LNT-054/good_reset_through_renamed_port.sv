// sverity-args: --top top
// The child's `init` is driven by the top reset `rst`: the net, not the local name, is the reset.
// expect-none: LNT-054
module child(input logic clk, init, d, output logic y);
  logic q;
  always_ff @(posedge clk) if (init) q <= 1'b0; else q <= d;
  always_comb begin
    if (q) y = d; else y = 1'b0;
  end
endmodule
module top(input logic clk, rst, d, output logic y);
  child u(.clk(clk), .init(rst), .d(d), .y(y));
endmodule
