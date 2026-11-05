// sverity-args: --top top --sdc @DIR@/unknown_streaming_write.sdc
// A streaming target writes m as surely as a plain assignment does.
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-028 ERROR
module top(input logic a, b, output logic q);
  logic m;
  initial {<<{m}} = 1'b1;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (m) q <= source_q;
endmodule
