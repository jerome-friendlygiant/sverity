// sverity-args: --top top
// revert: j <= 4 => j < 4
// revert-expect-none: LNT-006
// The loop reads entry 4 of a four-entry array on its last iteration: proven, so reported by default.
// expect: LNT-006 ERROR 9
module top(input logic [7:0] d [4], output logic [7:0] x);
  always_comb begin
    x = '0;
    for (int j = 0; j <= 4; j++) x ^= d[j];
  end
endmodule
