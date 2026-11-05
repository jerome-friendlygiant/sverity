// sverity-args: --top top --sdc @DIR@/boundary_sibling_instance.sdc
// A value on u0 removes only u0's branch: its sibling keeps its crossing.
// expect: CST-014 WARNING
// expect: LNT-008 WARNING
// expect: CDC-028 ERROR
module leaf(input logic a, b, output logic q);
  logic m;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (m) q <= source_q;
endmodule
module top(input logic a, b, output logic q0, q1);
  leaf u0(.a, .b, .q(q0));
  leaf u1(.a, .b, .q(q1));
endmodule
