// sverity-args: --top top
// The write is live for i = 2 and 3 only: bits 1:0 of x are never driven, which is not "every write is dead".
// expect: LNT-008 WARNING 5
module top(input logic [3:0] a, output logic [3:0] y);
  logic [3:0] x;
  for (genvar i = 0; i < 4; i++) begin : g
    always_comb if (i >= 2) x[i] = a[i];
  end
  assign y = x;
endmodule
