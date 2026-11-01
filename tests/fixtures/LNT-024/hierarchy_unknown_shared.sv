// sverity-args: --top top
// expect: LNT-024 WARNING 4
// expect-count: LNT-024 WARNING 2
module leaf(input logic [1:0] i, input logic a, output logic [1:0] q);
  always_comb begin
    q[0] = 1'b0;
    q[i] = a;
  end
endmodule
module top(input logic [1:0] i, input logic a, output logic [1:0] y, output logic [1:0] z);
  leaf u0 (.i(i), .a(a), .q(y));
  leaf u1 (.i(i), .a(a), .q(z));
endmodule
