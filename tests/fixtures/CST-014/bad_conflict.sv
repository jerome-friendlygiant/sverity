// sverity-args: --top top --sdc @DIR@/bad_conflict.sdc
// expect: CST-018 WARNING
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-003 ERROR
module top(input logic a, b, mode, output logic q);
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) q <= mode ? source_q : 1'b0;
endmodule
