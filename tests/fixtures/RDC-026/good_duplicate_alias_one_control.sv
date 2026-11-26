// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// b_alias is an exact alias of a, the same source and polarity: the two controls are one control, so there is no set/clear conflict.
// expect-none: RDC-026
module top(input logic clka, a, c, d, output logic q);
  wire b_alias = a;
  always_ff @(posedge clka or negedge a or negedge b_alias)
    if (!a) q <= 0; else if (!b_alias) q <= 1; else q <= d;
endmodule
