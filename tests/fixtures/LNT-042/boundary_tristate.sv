// sverity-args: --top top
// expect-none: LNT-042
// expect-none: LNT-009
module leaf(input logic en, input logic a, output tri o);
  assign o = en ? a : 1'bz;
endmodule
module top(input logic en0, input logic en1, input logic a, input logic b, output logic y);
  tri w;
  leaf u0 (.en(en0), .a(a), .o(w));
  leaf u1 (.en(en1), .a(b), .o(w));
  assign y = w;
endmodule
