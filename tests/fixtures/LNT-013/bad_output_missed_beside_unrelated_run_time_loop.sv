// sverity-args: --top top
// revert: if (v[0]) o = acc; => o = acc;
// revert-expect-none: LNT-013
// The loop count is a run-time value, so the loop is walked once, but it touches only `acc`: that `o` is not assigned when v[0] is 0 is certain.
// expect: LNT-013 WARNING 8
// expect-confidence: LNT-013 established 8
module top(input logic [7:0] x, input logic [3:0] n, output logic [7:0] y);
  task automatic t(input logic [7:0] v, input logic [3:0] k, output logic [7:0] o);
    logic [7:0] acc;
    acc = '0;
    for (logic [3:0] i = 0; i < k; i++) acc = acc + v;
    if (v[0]) o = acc;
  endtask
  always_comb t(x, n, y);
endmodule
