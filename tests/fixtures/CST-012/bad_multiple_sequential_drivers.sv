// sverity-args: --top top --sdc @DIR@/bad_multiple_sequential_drivers.sdc
// A second block clocks q asynchronously to the declared capture clock: a violation whichever block comes first.
// expect: CST-012 ERROR
// expect-none: CST-012 INFO
// expect-coverage: constraints INCOMPLETE
module top(input logic a, input logic b, output logic q);
  always @(posedge a) q <= ~q;
  always @(posedge b) q <= ~q;
endmodule
