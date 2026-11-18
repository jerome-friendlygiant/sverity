// sverity-args: --top top
// A later initializer changes the counter before the first body evaluation.
// expect: LNT-005 WARNING 9
// expect-none: LNT-005 INFO
module top(output logic [2:0] y);
  int i, j;
  always_comb begin
    y = 0;
    for (i = 0, j = --i; i < 4; i++) y = i;
  end
endmodule
