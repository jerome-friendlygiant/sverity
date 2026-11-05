// sverity-args: --top top --sdc @DIR@/unknown_second_driver_in_child.sdc
// The child also drives m, and this wired-OR holds 1 whatever en is: fixing en proves nothing about m.
// expect: CST-009 WARNING
// expect: CDC-028 ERROR
module leaf(output wire o);
  assign o = 1'b1;
endmodule
module top(input wire a, b, en, output logic q);
  wor m;
  assign m = en;
  leaf u(m);
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (m) q <= source_q;
endmodule
