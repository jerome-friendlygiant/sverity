// sverity-args: --top top
// A block-local temporary written before it is read, in a clocked block: order is proved, no loop.
// expect-none: LNT-012
module top(input logic clk, a, b, output logic q);
  always_ff @(posedge clk) begin
    logic t;
    t = a;
    t = t ^ b;
    q <= t;
  end
endmodule
