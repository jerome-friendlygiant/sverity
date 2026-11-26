// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The X literal sits in an intermediate net of the reset's fan-in, not in its final driver: the combination is Unresolved.
// expect-unresolved: RDC-008
module top(input logic clka, rst_n, por_rst_n, d, output logic q);
  wire m = rst_n & 1'bx;
  wire rr = m & por_rst_n;
  always_ff @(posedge clka or negedge rr) if (!rr) q <= 1'b0; else q <= d;
endmodule
