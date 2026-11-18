// sverity-args: --top top
// The write's macro is defined before the break's, but the break's is invoked first: order follows the invocation, never the
// definition. The break reads an automatic local, so which iterations write is not determinable.
// expect: LNT-009 WARNING 13
`define WRITE v[i] = c;
`define STOP if (k == 3) break;
module top(input c, output [3:0] y);
  reg [3:0] v;
  always @(c) for (int i = 0; i < 4; i++) begin
    automatic int k = i;
    `STOP `WRITE
  end
  always @(c) v[3] = 0;
  assign y = v;
endmodule
