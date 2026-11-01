// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// XOR of two resets: the combination is not an AND/OR, so the semantics are reported as unclassified (Unresolved), never safe
// expect: RDC-008 WARNING
module top(input logic clka, rst_a_n, rst_b_n, d, output logic q);
  logic r;
  assign r = rst_a_n ^ rst_b_n;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
