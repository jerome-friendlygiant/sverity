// sverity-args: --top top --sdc @DIR@/bad_multiple_sequential_drivers.sdc
// bad_multiple_sequential_drivers with the blocks swapped: source order must not change the verdict.
// expect: CST-012 ERROR
// expect-none: CST-012 INFO
// expect-coverage: constraints INCOMPLETE
module top(input logic a, input logic b, output logic q);
  always @(posedge b) q <= ~q;
  always @(posedge a) q <= ~q;
endmodule
