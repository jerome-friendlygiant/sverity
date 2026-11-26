// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The select sel is reset from a net nothing drives, so whether its reset domain differs from the receiver's cannot be decided: Unresolved.
// expect-unresolved: RDC-017
module top(input logic clka, d, rst_n, output logic q);
  logic sel;
  wire opaque_rst;
  always_ff @(posedge clka or negedge opaque_rst) if (!opaque_rst) sel <= 1'b0; else sel <= d;
  wire r = rst_n | sel;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
