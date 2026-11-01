// sverity-args: --top top
// The loop closes through a child module's ports.
// expect: LNT-012 ERROR
module inv(input logic a, output logic y);
  assign y = ~a;
endmodule
module top(input logic en, output logic o);
  logic w;
  inv u(.a(w & en), .y(w));
  assign o = w;
endmodule
