// sverity-args: --top top
// A default assignment before the condition covers the path where it is false: no latch, at the edge of the rule.
// expect-none: LNT-013
module top(input logic en, input logic d, output logic q);
  always_comb begin
    q = 1'b0;
    if (en) q = d;
  end
endmodule
