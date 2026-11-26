// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Control a clears q only when c is set: with c low, asserting a holds the value, so whether a forces a value is not established (Unresolved), not an Established set/clear conflict.
// expect-unresolved: RDC-026
module top(input logic clka, a, b, c, d, output logic q);
  always_ff @(posedge clka or negedge a or negedge b)
    if (!a) begin if (c) q <= 0; end else if (!b) q <= 1; else q <= d;
endmodule
