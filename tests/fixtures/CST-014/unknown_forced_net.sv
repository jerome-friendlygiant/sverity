// sverity-args: --top top --sdc @DIR@/unknown_forced_net.sdc
// A force holds m at 1, so its driver proves nothing.
// expect: CST-009 WARNING
// expect: CDC-028 ERROR
module top(input wire a, b, en, output logic q);
  wire m = en;
  initial force m = 1'b1;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (m) q <= source_q;
endmodule
