// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Boundary: two identified clock leaves are sufficient for the unconsumed top-level clock-output predicate.
// expect: CDC-019 WARNING
// expect-unresolved: CDC-019
// expect-coverage: clock-extraction INCOMPLETE
module top(input logic clka, clkb, sel, output logic clk_out);
  assign clk_out = sel ? clka : clkb;
endmodule
