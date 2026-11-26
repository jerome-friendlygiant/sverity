// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// One operand of the reset combination is a driver cycle (a = b, b = a): what it carries is not known, so the combination is Unresolved.
// expect-unresolved: RDC-008
module top(input logic clka, rst_n, por_rst_n, d, output logic q);
  wire a, b; assign a = b; assign b = a;
  wire rr = rst_n & por_rst_n & a;
  always_ff @(posedge clka or negedge rr) if (!rr) q <= 1'b0; else q <= d;
endmodule
