// sverity-args: --top top
// expect: LNT-042 WARNING 9
module leaf(input logic a, output logic o);
  assign o = a;
endmodule
module top(input logic a, input logic b, output logic y);
  wire w;
  leaf u0 (.a(a), .o(w));
  leaf u1 (.a(b), .o(w));
  assign y = w;
endmodule
