// sverity-args: --top top
// Every bit of x is written: the arm `i < 2` is dead for i = 2 and 3, the other for i = 0 and 1. A write site is dead only when it is
// dead in every elaborated instance that holds it.
// expect-none: LNT-008
module top(input logic [3:0] a, output logic [3:0] y);
  logic [3:0] x;
  for (genvar i = 0; i < 4; i++) begin : g
    always_comb if (i < 2) x[i] = a[i]; else x[i] = 1'b0;
  end
  assign y = x;
endmodule
