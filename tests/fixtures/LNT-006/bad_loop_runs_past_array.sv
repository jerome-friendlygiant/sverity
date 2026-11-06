// sverity-args: --top top --option LNT-006.dynamic_index=true
// revert: j <= 4 => j < 4
// revert-expect-none: LNT-006
// The loop reads entry 4 of a four-entry array.
// expect: LNT-006 WARNING 9
module top(input logic [7:0] d [4], output logic [7:0] x);
  always_comb begin
    x = '0;
    for (int j = 0; j <= 4; j++) x ^= d[j];
  end
endmodule
