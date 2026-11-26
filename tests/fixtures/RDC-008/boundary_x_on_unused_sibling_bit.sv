// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The X literal drives only the unused sibling bit r[0]; the selected bit r[1] is a clean AND of two resets: a Heuristic finding, no RDC-009.
// expect: RDC-008 WARNING
// expect-none: RDC-009 WARNING
module top(input logic clka, rst_n, por_rst_n, sw_rst_n, d, output logic q);
  wire [1:0] r = {rst_n & por_rst_n, sw_rst_n & 1'bx};
  wire rr = r[1];
  always_ff @(posedge clka or negedge rr) if (!rr) q <= 1'b0; else q <= d;
endmodule
