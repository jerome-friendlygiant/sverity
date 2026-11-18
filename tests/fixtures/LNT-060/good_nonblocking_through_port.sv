// sverity-args: --top top
// The same child with a nonblocking write: the parent reads the value from before the edge.
// expect-none: LNT-060
module child(input logic clk, input logic [3:0] d, output logic [3:0] o);
  always_ff @(posedge clk) o <= d;
endmodule
module top(input logic clk, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] w;
  child c (.clk(clk), .d(d), .o(w));
  always_ff @(posedge clk) q <= w;
endmodule
