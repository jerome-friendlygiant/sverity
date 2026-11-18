// sverity-args: --top top
// The child writes its output with a blocking assignment on the clock edge; the parent reads the connected net on the same edge.
// expect: LNT-060 WARNING 10
module child(input logic clk, input logic [3:0] d, output logic [3:0] o);
  always_ff @(posedge clk) o = d;
endmodule
module top(input logic clk, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] w;
  child c (.clk(clk), .d(d), .o(w));
  always_ff @(posedge clk) q <= w;
endmodule
