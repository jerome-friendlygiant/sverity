// sverity-args: --top top --sdc @DIR@/unknown_struct_member_write.sdc
// Writing s.m writes s, so s is not undriven, and a struct is not a plain signal.
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-028 ERROR
module top(input logic a, b, output logic q);
  struct packed { logic m; } s;
  initial s.m = 1'b1;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (s) q <= source_q;
endmodule
