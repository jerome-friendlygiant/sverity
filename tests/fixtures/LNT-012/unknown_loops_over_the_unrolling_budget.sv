// sverity-args: --top top
// Three nested loops of 16, each under the per-loop limit but 4096 body walks in all: the inner ones are not unrolled (the walk would
// cost memory in proportion to the product), so the feedback through fb is possible but not proven.
// expect: LNT-012 WARNING
// expect-none: LNT-012 ERROR
module top(input logic [15:0] x, output logic [15:0] y);
  logic [15:0] fb;
  always_comb begin
    y = '0;
    for (int i = 0; i < 16; i++)
      for (int j = 0; j < 16; j++)
        for (int k = 0; k < 16; k++)
          y[i] = y[i] ^ (x[j] & fb[k]);
  end
  assign fb = y;
endmodule
