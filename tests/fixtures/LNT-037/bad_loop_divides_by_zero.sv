// sverity-args: --top top --option LNT-037.dynamic_divisor=true
// revert: int i = 0; => int i = 1;
// revert-expect-none: LNT-037
// The first iteration divides by zero.
// expect: LNT-037 WARNING 9
module top(input logic [7:0] n, output logic [7:0] x);
  always_comb begin
    x = '0;
    for (int i = 0; i < 4; i++) x ^= n % i;
  end
endmodule
