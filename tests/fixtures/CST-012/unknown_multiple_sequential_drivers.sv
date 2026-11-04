// sverity-args: --top top --sdc @DIR@/unknown_multiple_sequential_drivers.sdc
// A second block clocks q asynchronously, and the model keeps only the first clock: never "consistent".
// expect: CST-012 WARNING
// expect-none: CST-012 INFO
// expect-coverage: constraints INCOMPLETE
module top(input logic a, input logic b, output logic q);
  always @(posedge a) q <= ~q;
  always @(posedge b) q <= ~q;
endmodule
