// sverity-args: --top top
// A non-constant index writes the net an output port drives: overlap cannot be decided, so it stays Unresolved.
// expect: LNT-009 WARNING
// expect: LNT-042 WARNING
module leaf(input logic a, output logic [1:0] o);
  assign o = {a, a};
endmodule
module top(input logic a, input logic [0:0] i, output logic [3:0] y);
  logic [3:0] w;
  leaf u0 (.a(a), .o(w[1:0]));
  always_comb w[i + 2] = a;
  assign y = w;
endmodule
