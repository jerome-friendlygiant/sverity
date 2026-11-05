// sverity-args: --top top --sdc @DIR@/unknown_multiple_drivers.sdc
// Two drivers, one of them agreeing: the resolved value is not established.
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-028 ERROR
module top(input logic a, b, en, output logic q);
  wire m;
  assign m = en;
  assign m = 1'b0;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (m) q <= source_q;
endmodule
