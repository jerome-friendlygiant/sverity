// sverity-args: --top top
// The X literal drives bit 1 of r, and the reset rr reads that bit through a declared ascending range: the X reaches the
// asynchronous root, so the combination is Unresolved (RDC-008), not classified.
// expect-unresolved: RDC-008
module top(input wire clka, rst_n, por_rst_n, sw_rst_n, d, output logic q);
  wire [0:3] r;
  assign r[2] = rst_n & por_rst_n;
  assign r[1] = sw_rst_n & 1'bx;
  wire rr = r[1] & por_rst_n;
  always_ff @(posedge clka or negedge rr) if (!rr) q <= 0; else q <= d;
endmodule
