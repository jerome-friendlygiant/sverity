// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Unsafe lookalike of good_reset_propagation_flag_into_sync: source f has data D (da) instead of a constant, so RDC-005 must still warn.
// expect: RDC-005 WARNING
module top(input logic clka, clkb, rsta, rstb, da, output logic ob);
  logic f, b1, b2;
  always_ff @(posedge clka or posedge rsta) if (rsta) f <= 1'b1; else f <= da;
  always_ff @(posedge clkb) begin b1 <= f; b2 <= b1; end
  assign ob = b2;
endmodule
