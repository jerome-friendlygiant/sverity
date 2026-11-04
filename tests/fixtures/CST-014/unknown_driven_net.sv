// sverity-args: --top top --sdc @DIR@/unknown_driven_net.sdc
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-003 ERROR
module top(input logic a, b, en, output logic q);
  wire mode = en;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (mode) q <= source_q;
endmodule
