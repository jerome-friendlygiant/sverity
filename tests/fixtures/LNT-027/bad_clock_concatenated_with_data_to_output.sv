// sverity-args: --top top
// The clock rides in one bit of a vector whose other bit is data: the port carries clock and data together.
// expect: LNT-027 WARNING 7
// expect-none: LNT-027 INFO
module top(input logic clk, d, output logic q, output logic [1:0] o);
  always_ff @(posedge clk) q <= d;
  assign o = {clk, d};
endmodule
