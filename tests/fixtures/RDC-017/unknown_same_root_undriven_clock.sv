// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The select's clock c is undriven, so its domain is not identified even though its reset sources equal the receiver's. An unidentified clock is Unresolved, not a clean match.
// expect-unresolved: RDC-017
module top(input logic clka, d, rst_n, output logic q);
  logic sel;
  wire c, r;
  always_ff @(posedge c or negedge r) if (!r) sel <= 1'b0; else sel <= d;
  assign r = rst_n | sel;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
