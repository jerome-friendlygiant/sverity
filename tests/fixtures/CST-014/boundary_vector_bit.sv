// sverity-args: --top top --sdc @DIR@/boundary_vector_bit.sdc
// expect: CST-014 WARNING
// Only mode[0] is fixed: the crossing guarded by the unfixed mode[1] must stay reported.
// expect: CDC-028 ERROR
module top(input logic a, b, input logic [1:0] mode, output logic q, r);
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (mode[0]) q <= source_q;
  always_ff @(posedge b) if (mode[1]) r <= source_q;
endmodule
