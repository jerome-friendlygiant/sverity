// sverity-args: --top top --sdc @DIR@/bad_vector_equality_selected_mode.sdc
// expect: CST-014 WARNING
// Both bits are 1, so the mode 2'b11 is selected and its crossing is reported.
// expect: CDC-003 ERROR
module top(input logic a, b, input logic [1:0] mode, output logic q);
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (mode == 2'b11) q <= source_q;
endmodule
