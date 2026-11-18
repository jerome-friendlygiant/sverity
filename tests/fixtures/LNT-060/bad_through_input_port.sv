// sverity-args: --top top
// The parent writes `a` with a blocking assignment on the clock edge; the child reads it through its input port on the same edge.
// expect: LNT-060 WARNING 5
module sink(input logic clk, input logic [3:0] i, output logic [3:0] q);
  always_ff @(posedge clk) q <= i;
endmodule
module top(input logic clk, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] a;
  always_ff @(posedge clk) a = d;
  sink s (.clk(clk), .i(a), .q(q));
endmodule
