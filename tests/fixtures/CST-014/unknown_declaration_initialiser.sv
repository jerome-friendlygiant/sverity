// sverity-args: --top top --sdc @DIR@/unknown_declaration_initialiser.sdc
// The initialiser sets m to 1, so nothing here is undriven.
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-028 ERROR
module top(input logic a, b, output logic q);
  logic m = 1'b1;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (m) q <= source_q;
endmodule
