// sverity-args: --top top --sdc @DIR@/bad_vector_bit_beside_data.sdc
// expect: CST-014 WARNING
// A fixed bit in a concatenation does not fix the other bit, which still carries the crossing.
// expect: CDC-003 ERROR
module top(input logic a, b, input logic [1:0] mode, output logic [1:0] q);
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) q <= {mode[1], source_q};
endmodule
