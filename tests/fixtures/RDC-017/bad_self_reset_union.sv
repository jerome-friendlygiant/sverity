// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The select sel is reset by r (which it gates) and by aux_rst_n. The receiver q is reset by r alone: sel's own sources must not
// count as a receiver, or the extra reset aux_rst_n would be masked. Expected: sel's domain differs from the receiver's.
// expect: RDC-017 WARNING
// expect-confidence: RDC-017 heuristic
module top(input logic clka, d, rst_n, aux_rst_n, output logic q);
  wire r;
  logic sel;
  always_ff @(posedge clka or negedge r or negedge aux_rst_n) if (!r) sel <= 1'b0; else if (!aux_rst_n) sel <= 1'b0; else sel <= d;
  assign r = rst_n | sel;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
