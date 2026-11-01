// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// (a & b) | (a & c) is not "asserts on any source": b alone does not reset. The shape is not classified (Unresolved).
// expect: RDC-008 WARNING
module top(input logic clka, rst_a_n, rst_b_n, rst_c_n, d, output logic q);
  logic r;
  assign r = (rst_a_n & rst_b_n) | (rst_a_n & rst_c_n);
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
