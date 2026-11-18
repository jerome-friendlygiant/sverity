// sverity-args: --top top
// The counter is `$clog2(N + 1)` bits wide, one bit at N = 1. Its increment fits only where a guard keeps the counter below N; under an
// unrelated condition the sum needs a bit the target has not.
// revert: if (go) d = q + 1; => if (q < N) d = q + 1;
// revert-expect-none: LNT-057
// expect: LNT-057 WARNING 11
module top #(parameter int N = 1) (input logic clk, go, output logic y);
  logic [$clog2(N + 1)-1:0] d, q;
  always_comb begin
    d = q;
    if (go) d = q + 1;
  end
  always_ff @(posedge clk) q <= d;
  assign y = q[0];
endmodule
