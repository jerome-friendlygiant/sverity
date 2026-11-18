// sverity-args: --top top
// revert: i < 8 => i < 7
// revert-expect-none: LNT-006
// `a[i+1]` reaches 8 on the last iteration, which always runs: proven from the range of the expression, reported by default.
// expect: LNT-006 ERROR 10
module top(input logic [7:0] d, output logic [7:0] y);
  logic [7:0] a;
  always_comb begin
    a = '0;
    for (int i = 0; i < 8; i++) a[i+1] = d[i];
    y = a;
  end
endmodule
