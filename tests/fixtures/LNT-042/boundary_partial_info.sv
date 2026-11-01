// sverity-args: --top top
// expect: LNT-042 INFO 8
module leaf(input logic a, output logic [1:0] o);
  assign o = {a, a};
endmodule
module top(input logic a, output logic y);
  wire [3:0] w;
  leaf u (.a(a), .o(w[1:0]));
  assign y = w[0];
endmodule
