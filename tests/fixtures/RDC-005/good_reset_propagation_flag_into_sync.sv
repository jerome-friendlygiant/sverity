// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A reset-propagation flag (async set by rsta, released D constant 0) sampled as sole data by a 2-stage CDC data synchroniser in clkb is exempt from RDC-005.
// expect-none: RDC-005
module top(input logic clka, clkb, rsta, rstb, output logic ob);
  logic f, b1, b2;
  always_ff @(posedge clka or posedge rsta) if (rsta) f <= 1'b1; else f <= 1'b0;
  always_ff @(posedge clkb) begin b1 <= f; b2 <= b1; end
  assign ob = b2;
endmodule
