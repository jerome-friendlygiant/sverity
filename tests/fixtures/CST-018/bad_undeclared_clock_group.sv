// sverity-args: --top top --sdc @DIR@/undeclared_clock_group.sdc
// set_clock_groups naming an undeclared clock must trigger CST-018 WARNING Unresolved and mark constraints INCOMPLETE.
// expect: CST-018 WARNING
// expect-unresolved: CST-018
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, d, output logic q);
  always_ff @(posedge clka) q <= d;
endmodule
