// sverity-args: --top top --sdc @DIR@/unknown_driven_contradicts_driver.sdc
// The RTL drives 1 and the constraint says 0: a mistaken constraint must not delete the crossing.
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-028 ERROR
module top(input logic a, b, output logic q);
  wire m = 1'b1;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (m) q <= source_q;
endmodule
