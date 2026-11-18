// sverity-args: --top top
// The break comes from a macro expansion, so its order against the write is not known from source locations: which iterations
// write, and so whether the writes overlap, is not determinable.
// expect: LNT-009 WARNING 12
`define STOP if (k == 3) break;
module top(input c, output [3:0] y);
  reg [3:0] v;
  always @(c) for (int i = 0; i < 4; i++) begin
    automatic int k = i;
    `STOP v[i] = c;
  end
  always @(c) v[3] = 0;
  assign y = v;
endmodule
