// sverity-args: --top top
// The break at k == 3 runs before the write of that iteration: v[3] is not written by the loop, so no overlap is claimed.
// expect: LNT-009 WARNING 11
module top(input c, output [3:0] y);
  reg [3:0] v;
  always @(c) for (int i = 0; i < 4; i++) begin
    automatic int k = i;
    if (k == 3) break;
    v[i] = c;
  end
  always @(c) v[3] = 0;
  assign y = v;
endmodule
