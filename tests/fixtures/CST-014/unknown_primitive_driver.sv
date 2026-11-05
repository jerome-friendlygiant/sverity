// sverity-args: --top top --sdc @DIR@/unknown_primitive_driver.sdc
// A gate drives m, and the model does not follow primitives: not undriven.
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-028 ERROR
module top(input logic a, b, en, output logic q);
  wire m;
  buf (m, en);
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (m) q <= source_q;
endmodule
