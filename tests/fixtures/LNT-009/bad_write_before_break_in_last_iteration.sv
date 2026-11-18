// sverity-args: --top top
// The break at k == 3 stops iteration 3 after its write: v[3] is written there, and by the second process.
// expect: LNT-009 ERROR 11
module top(input c, output [3:0] y);
  reg [3:0] v;
  always @(c) for (int i = 0; i < 4; i++) begin
    automatic int k = i;
    v[i] = c;
    if (k == 3) break;
  end
  always @(c) v[3] = 0;
  assign y = v;
endmodule
