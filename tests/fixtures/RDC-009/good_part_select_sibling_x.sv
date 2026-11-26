// sverity-args: --top top
// The X literal drives bit 0 of r; the reset rr is bit 1 only, a homogeneous AND of two active-low resets. The X on a sibling bit
// does not reach it, so there is no gate (RDC-009) and the combination is Heuristic (RDC-008).
// expect-none: RDC-009
// expect: RDC-008 WARNING
module top(input wire clka, rst_n, por_rst_n, sw_rst_n, d, output logic q);
  wire [3:0] r;
  assign r[1:0] = {rst_n & por_rst_n, sw_rst_n & 1'bx};
  wire rr = r[1];
  always_ff @(posedge clka or negedge rr) if (!rr) q <= 0; else q <= d;
endmodule
