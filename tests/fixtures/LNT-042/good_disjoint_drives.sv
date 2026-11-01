// sverity-args: --top top
// expect-none: LNT-042
module leaf(input logic a, output logic o);
  assign o = a;
endmodule
module top(input logic a, input logic b, output logic y);
  wire [1:0] w;
  leaf u0 (.a(a), .o(w[0]));
  leaf u1 (.a(b), .o(w[1]));
  assign y = w[0] & w[1];
endmodule
