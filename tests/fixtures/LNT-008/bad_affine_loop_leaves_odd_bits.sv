// sverity-args: --top top
// The loop writes v[2*i] only: the proof places the even bits, so the odd ones are a certain gap.
// expect: LNT-008 WARNING 5
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  always_comb begin for (int i = 0; i < 4; i++) v[2*i] = c; end
  assign y = v;
endmodule
