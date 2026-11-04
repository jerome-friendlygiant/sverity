// sverity-args: --top top --sdc @DIR@/unknown_driven_input.sdc
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-003 ERROR
module top(input wire a, b, en, mode, output logic q);
  assign mode = en;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (mode) q <= source_q;
endmodule
