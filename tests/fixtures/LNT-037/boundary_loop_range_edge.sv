// sverity-args: --top top --option LNT-037.dynamic_divisor=true
// A loop variable that starts at 1 never divides by zero; one that starts at 0 does on its first iteration.
// expect: LNT-037 WARNING 10
// expect-count: LNT-037 WARNING 1
module top(input logic [7:0] n, output logic [7:0] x, y);
  always_comb begin
    x = '0;
    y = '0;
    for (int i = 1; i < 4; i++) x ^= n % i;
    for (int j = 0; j < 4; j++) y ^= n % j;
  end
endmodule
