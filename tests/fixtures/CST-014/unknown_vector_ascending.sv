// sverity-args: --top top --sdc @DIR@/unknown_vector_ascending.sdc
// mode[0] is the most significant bit here, so offset and index differ.
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-028 ERROR
module top(input logic a, b, input logic [0:1] mode, output logic q);
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (mode[0]) q <= source_q;
endmodule
