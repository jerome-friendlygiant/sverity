// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// q is cleared under !a and set under !b, but the clear is nested under c, which is not one of q's controls: whether a forces q is not established.
// The other register p is reset by c, which does not make c's use on q a reset use.
// expect-unresolved: RDC-026
module top(input logic clka, a, b, c, d, output logic q, p);
  always_ff @(posedge clka or negedge a or negedge b)
    if (!a) begin
      if (c) q <= 0;
    end else if (!b) q <= 1;
    else q <= d;
  always_ff @(posedge clka or posedge c)
    if (c) p <= 0; else p <= d;
endmodule
