// sverity-args: --top top
// expect: LNT-024 WARNING 5
// expect: LNT-008 WARNING 12
// expect: LNT-042 INFO 13
module leaf(input logic [1:0] i, input logic a, output logic [1:0] o);
  always_comb begin
    o[0] = 1'b0;
    o[i] = a;
  end
endmodule
module top(input logic [1:0] i, input logic a, output logic y);
  wire [3:0] w;
  leaf u (.i(i), .a(a), .o(w[1:0]));
  assign y = w[0];
endmodule
