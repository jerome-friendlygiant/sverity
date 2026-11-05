// sverity-args: --top top --sdc @DIR@/unknown_vector.sdc
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-028 ERROR
module top(input logic a, b, input logic [1:0] mode, output logic q);
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (mode[0]) q <= source_q;
endmodule
