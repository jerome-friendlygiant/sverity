// sverity-args: --top top --sdc @DIR@/undeclared_get_clocks.sdc
// Path exception using [get_clocks missing_clk] for an undeclared clock must trigger CST-018 WARNING Unresolved and mark constraints INCOMPLETE.
// expect: CST-018 WARNING
// expect-unresolved: CST-018
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, d, output logic q);
  always_ff @(posedge clka) q <= d;
endmodule
