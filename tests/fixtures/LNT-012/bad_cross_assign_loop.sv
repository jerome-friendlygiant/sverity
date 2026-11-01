// sverity-args: --top top
// Two continuous assignments feeding each other (an SR-latch style loop): an established combinational cycle.
// expect: LNT-012 ERROR
module top(input logic s, r, output logic q, qn);
  assign q  = ~(s & qn);
  assign qn = ~(r & q);
endmodule
