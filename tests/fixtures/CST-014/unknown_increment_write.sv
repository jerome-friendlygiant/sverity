// sverity-args: --top top --sdc @DIR@/unknown_increment_write.sdc
// An increment is a write: m starts at 0 and becomes 1.
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-028 ERROR
module top(input logic a, b, output logic q);
  bit m;
  initial m++;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (m) q <= source_q;
endmodule
