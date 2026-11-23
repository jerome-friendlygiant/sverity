// sverity-args: --top top --sdc @DIR@/input_unidentified_clock.sdc
// A primary input captured by a register with an unidentified clock must trigger CST-004 WARNING Unresolved and clock-relationships INCOMPLETE.
// expect: CST-004 WARNING
// expect-unresolved: CST-004
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clka, clkb, d, output logic q);
  always_ff @(posedge clka or negedge clkb) q <= d;
endmodule
