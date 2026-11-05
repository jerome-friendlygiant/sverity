// sverity-args: --top top --sdc @DIR@/good_vector_equality_other_mode.sdc
// expect: CST-014 WARNING
// Both bits are 1, so the branch for 2'b01 is removed and the run says this mode is all it covers.
// expect-none: CDC-028
// expect-coverage: case-analysis-modes INCOMPLETE
module top(input logic a, b, input logic [1:0] mode, output logic q);
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (mode == 2'b01) q <= source_q;
endmodule
